#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "kernel.h"
#include "kernelfs.h"
#include "log.h"
#include "procman.h"
#include "wrapper.h"

#define procManProcessInstructionCounterMax ((1u)<<16) // TODO: On arduino this needs 32 bit
#define procManProcessInstructionCounterMaxMinusOne (((1u)<<16)-1) // TODO: On arduino this only needs 16 bit but needs calculating differently
#define procManProcessTickInstructionsPerTick 8 // Generally a higher value causes faster execution, but decreased responsiveness if many processes running
#define procManTicksPerInstructionCounterReset (3*1024) // must not exceed procManProcessInstructionCounterMax/procManProcessTickInstructionsPerTick, which is currently 8k, target is to reset roughly every 10s

#define ProcManSignalHandlerInvalid 0

typedef enum {
	ProcManProcessStateUnused,
	ProcManProcessStateActive,
	ProcManProcessStateWaitingWaitpid,
	ProcManProcessStateWaitingRead,
} ProcManProcessState;

#define ARGVMAX 4
typedef struct {
	KernelFsFileOffset argv[ARGVMAX]; // Pointers into start of ramFd
	char pwd[KernelFsPathMax]; // set to '/' when init is called
	char path[KernelFsPathMax]; // set to '/bin' when init is called
	KernelFsFd stdioFd; // set to KernelFsFdInvalid when init is called
} ProcManProcessEnvVars;

typedef struct {
	ByteCodeWord regs[BytecodeRegisterNB];
	KernelFsFileOffset signalHandlers[ByteCodeSignalIdNB];
	ProcManProcessEnvVars envVars;
	uint16_t argvDataLen, ramLen;
	uint8_t ramFd;
	bool skipFlag; // skip next instruction?
} ProcManProcessProcData;

typedef struct {
	uint16_t instructionCounter; // reset regularly
	uint16_t waitingData16;
	KernelFsFd progmemFd, procFd;
	uint8_t state;
	uint8_t waitingData8;
} ProcManProcess;

typedef struct {
	ProcManProcess processes[ProcManPidMax];
	uint16_t ticksSinceLastInstructionCounterReset;
} ProcMan;
ProcMan procManData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid);
ProcManPid procManGetPidFromProcess(const ProcManProcess *process);
const char *procManGetExecPathFromProcess(const ProcManProcess *process);

ProcManPid procManFindUnusedPid(void);

bool procManProcessLoadProcData(const ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessStoreProcData(ProcManProcess *process, ProcManProcessProcData *procData);

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, uint8_t *value);
bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, ByteCodeWord *value);
bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, char *str, uint16_t len);
bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, uint8_t value);
bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, ByteCodeWord value);
bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, const char *str);

bool procManProcessGetArgvN(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char str[64]); // Returns false to indicate illegal memory operation. Always succeeds otherwise, but str may be 0 length.  TODO: Avoid hardcoded limit

bool procManProcessGetInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong *instruction);
bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong instruction, ProcManExitStatus *exitStatus);

void procManProcessFork(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData); // Returns false only on critical error (e.g. segfault), i.e. may return true even though exec operation itself failed

bool procManProcessRead(ProcManProcess *process, ProcManProcessProcData *procData);

void procManResetInstructionCounters(void);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void) {
	// Clear processes table
	for(int i=0; i<ProcManPidMax; ++i) {
		procManData.processes[i].state=ProcManProcessStateUnused;
		procManData.processes[i].progmemFd=KernelFsFdInvalid;
		procManData.processes[i].procFd=KernelFsFdInvalid;
		procManData.processes[i].instructionCounter=0;
	}

	// Clear other fields
	procManData.ticksSinceLastInstructionCounterReset=0;
}

void procManQuit(void) {
	// Kill all processes
	for(int i=0; i<ProcManPidMax; ++i)
		procManProcessKill(i, ProcManExitStatusKilled);
}

void procManTickAll(void) {
	// Run single tick for each process
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		procManProcessTick(pid);

	// Have we ran enough ticks to reset the instruction counters? (they are about to overflow)
	++procManData.ticksSinceLastInstructionCounterReset;
	if (procManData.ticksSinceLastInstructionCounterReset>=procManTicksPerInstructionCounterReset) {
		procManData.ticksSinceLastInstructionCounterReset=0;
		procManResetInstructionCounters();
	}
}

int procManGetProcessCount(void) {
	int count=0;
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		count+=(procManGetProcessByPid(pid)!=NULL);
	return count;
}

ProcManPid procManProcessNew(const char *programPath) {
	KernelFsFd ramFd=KernelFsFdInvalid;

	kernelLog(LogTypeInfo, "attempting to create new process at '%s'\n", programPath);

	// Find a PID for the new process
	ProcManPid pid=procManFindUnusedPid();
	if (pid==ProcManPidMax) {
		kernelLog(LogTypeWarning, "could not create new process - no spare PIDs\n");
		return ProcManPidMax;
	}

	// Construct tmp paths
	// TODO: Try others if exist
	char procPath[KernelFsPathMax], ramPath[KernelFsPathMax];
	sprintf(procPath, "/tmp/proc%u", pid);
	sprintf(ramPath, "/tmp/ram%u", pid);

	// Attempt to open program file
	procManData.processes[pid].progmemFd=kernelFsFileOpen(programPath);
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not create new process - could not open program at '%s'\n", programPath);
		goto error;
	}

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(procPath, sizeof(ProcManProcessProcData))) {
		kernelLog(LogTypeWarning, "could not create new process - could not create process data file at '%s' of size %u\n", procPath, sizeof(ProcManProcessProcData));
		goto error;
	}
	KernelFsFileOffset argvDataLen=1; // single byte for NULL terminator acting as all (empty) arguments
	if (!kernelFsFileCreateWithSize(ramPath, argvDataLen)) {
		kernelLog(LogTypeWarning, "could not create new process - could not create ram data file at '%s' of size %u\n", ramPath, argvDataLen);
		goto error;
	}

	// Attempt to open proc and ram files
	procManData.processes[pid].procFd=kernelFsFileOpen(procPath);
	if (procManData.processes[pid].procFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not create new process - could not open process data file at '%s'\n", procPath);
		goto error;
	}

	ramFd=kernelFsFileOpen(ramPath);
	if (ramFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not create new process - could not open ram data file at '%s'\n", ramPath);
		goto error;
	}

	// Initialise state
	procManData.processes[pid].state=ProcManProcessStateActive;
	procManData.processes[pid].instructionCounter=0;

	// Initialise proc file (and argv data in ram file)
	ProcManProcessProcData procData;
	procData.regs[ByteCodeRegisterIP]=0;
	for(uint16_t i=0; i<ByteCodeSignalIdNB; ++i)
		procData.signalHandlers[i]=ProcManSignalHandlerInvalid;
	procData.skipFlag=false;
	procData.envVars.stdioFd=KernelFsFdInvalid;
	procData.argvDataLen=argvDataLen;
	procData.ramLen=0;
	procData.ramFd=ramFd;
	strcpy(procData.envVars.pwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(procData.envVars.pwd, &dirname, &basename);
	assert(dirname==procData.envVars.pwd);

	strcpy(procData.envVars.path, "/home:/usr/bin:/bin:");

	uint8_t nullByte;
	if (kernelFsFileWriteOffset(ramFd, 0, &nullByte, 1)!=1) {
		kernelLog(LogTypeWarning, "could not create new process - could not write argv data into ram data file at '%s', fd %u\n", ramPath, ramFd);
		goto error;
	}
	for(unsigned i=0; i<ARGVMAX; ++i)
		procData.envVars.argv[i]=0;

	if (!procManProcessStoreProcData(&procManData.processes[pid], &procData)) {
		kernelLog(LogTypeWarning, "could not create new process - could not save process data file\n");
		goto error;
	}

	kernelLog(LogTypeInfo, "created new process '%s' with PID %u\n", programPath, pid);

	return pid;

	error:
	if (pid!=ProcManPidMax) {
		kernelFsFileClose(procManData.processes[pid].progmemFd);
		procManData.processes[pid].progmemFd=KernelFsFdInvalid;
		kernelFsFileClose(procManData.processes[pid].procFd);
		procManData.processes[pid].procFd=KernelFsFdInvalid;
		kernelFsFileDelete(procPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
		kernelFsFileClose(ramFd);
		kernelFsFileDelete(ramPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
		procManData.processes[pid].state=ProcManProcessStateUnused;
		procManData.processes[pid].instructionCounter=0;
	}

	return ProcManPidMax;
}

void procManProcessKill(ProcManPid pid, ProcManExitStatus exitStatus) {
	kernelLog(LogTypeInfo, "attempting to kill process %u with exit status %u\n", pid, exitStatus);

	// Not even open?
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL) {
		kernelLog(LogTypeWarning, "could not kill process %u - no such process\n", pid);
		return;
	}

	// Close files, deleting tmp ones
	KernelFsFd progmemFd=process->progmemFd;
	if (progmemFd!=KernelFsFdInvalid) {
		// progmemFd may be shared so check if anyone else is still using this one before closing it
		process->progmemFd=KernelFsFdInvalid;

		unsigned i;
		for(i=0; i<ProcManPidMax; ++i)
			if (procManData.processes[i].progmemFd==progmemFd)
				break;

		if (i==ProcManPidMax)
			kernelFsFileClose(progmemFd);
	}

	if (process->procFd!=KernelFsFdInvalid) {
		// Close and delete ram file
		ProcManProcessProcData procData;
		if (procManProcessLoadProcData(process, &procData)) {
			char ramPath[KernelFsPathMax];
			strcpy(ramPath, kernelFsGetFilePath(procData.ramFd));
			kernelFsFileClose(procData.ramFd);
			kernelFsFileDelete(ramPath);
		}

		// Close and delete proc file
		char procPath[KernelFsPathMax];
		strcpy(procPath, kernelFsGetFilePath(process->procFd));

		kernelFsFileClose(process->procFd);
		process->procFd=KernelFsFdInvalid;

		kernelFsFileDelete(procPath);
	}

	// Reset state
	process->state=ProcManProcessStateUnused;
	process->instructionCounter=0;

	kernelLog(LogTypeInfo, "killed process %u\n", pid);

	// Check if any processes are waiting due to waitpid syscall
	for(unsigned waiterPid=0; waiterPid<ProcManPidMax; ++waiterPid) {
		ProcManProcess *waiterProcess=procManGetProcessByPid(waiterPid);
		if (waiterProcess!=NULL && waiterProcess->state==ProcManProcessStateWaitingWaitpid && waiterProcess->waitingData8==pid) {
			// Bring this process back to life, storing the exit status into r0
			ProcManProcessProcData waiterProcData;
			if (!procManProcessLoadProcData(waiterProcess, &waiterProcData)) {
				kernelLog(LogTypeWarning, "process %u died - could not wake process %u from waitpid syscall (could not load proc data)\n", pid, waiterPid);
			} else {
				waiterProcData.regs[0]=exitStatus;
				if (!procManProcessStoreProcData(waiterProcess, &waiterProcData)) {
					kernelLog(LogTypeWarning, "process %u died - could not wake process %u from waitpid syscall (could not save proc data)\n", pid, waiterPid);
				} else {
					kernelLog(LogTypeInfo, "process %u died - woke process %u from waitpid syscall\n", pid, waiterPid);
					waiterProcess->state=ProcManProcessStateActive;
				}
			}
		}
	}
}

void procManProcessTick(ProcManPid pid) {
	ProcManExitStatus exitStatus=ProcManExitStatusKilled;

	// Find process from PID
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return;

	// Inspect state of the process
	ProcManProcessProcData procData;
	switch(process->state) {
		case ProcManProcessStateUnused: {
			// Process is unused (probably shouldn't pass the check above in this case anyway)
			return;
		} break;
		case ProcManProcessStateActive: {
			// Process is active, load process data ready to run next instruction
			if (!procManProcessLoadProcData(process, &procData)) {
				kernelLog(LogTypeWarning, "process %u tick (active) - could not load proc data, killing\n", pid);
				goto kill;
			}
		} break;
		case ProcManProcessStateWaitingWaitpid: {
			// Is this process waiting for a timeout, and that time has been reached?
			if (process->waitingData16>0 && millis()/1000>=process->waitingData16) {
				// It has - load process data so we can update the state and set r0 to indicate a timeout occured
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, "process %u tick (waitpid timeout) - could not load proc data, killing\n", pid);
					goto kill;
				}

				process->state=ProcManProcessStateActive;
				procData.regs[0]=ProcManExitStatusTimeout;
			} else {
				// Otherwise process stays waiting
				return;
			}
		} break;
		case ProcManProcessStateWaitingRead: {
			// Is data now available?
			if (kernelFsFileCanRead(process->waitingData8)) {
				// It is - load process data so we can update the state and read the data
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, "process %u tick (read available) - could not load proc data, killing\n", pid);
					goto kill;
				}

				process->state=ProcManProcessStateActive;
				procManProcessRead(process, &procData);
			} else {
				// Otherwise process stays waiting
				return;
			}
		} break;
	}

	// Run a few instructions
	for(unsigned instructionNum=0; instructionNum<procManProcessTickInstructionsPerTick; ++instructionNum) {
		// Run a single instruction
		BytecodeInstructionLong instruction;
		if (!procManProcessGetInstruction(process, &procData, &instruction)) {
			kernelLog(LogTypeWarning, "process %u tick - could not get instruction, killing\n", pid);
			goto kill;
		}

		// Are we meant to skip this instruction? (due to a previous skipN instruction)
		if (procData.skipFlag) {
			procData.skipFlag=false;

			// Read the next instruction instead
			if (!procManProcessGetInstruction(process, &procData, &instruction)) {
				kernelLog(LogTypeWarning, "process %u tick - could not get next instruction after skipping, killing\n", pid);
				goto kill;
			}
		}

		// Execute instruction
		if (!procManProcessExecInstruction(process, &procData, instruction, &exitStatus)) {
			kernelLog(LogTypeWarning, "process %u tick - could not exec instruction or returned false, killing\n", pid);
			goto kill;
		}

		// Increment instruction counter
		assert(process->instructionCounter<procManProcessInstructionCounterMaxMinusOne); // we reset often enough to prevent this
		++process->instructionCounter;

		// Has this process gone inactive?
		if (procManData.processes[pid].state!=ProcManProcessStateActive)
			break;
	}

	// Save tmp data
	if (!procManProcessStoreProcData(process, &procData)) {
		kernelLog(LogTypeWarning, "process %u tick - could not store proc data post tick, killing\n", pid);
		goto kill;
	}

	return;

	kill:
	procManProcessKill(pid, exitStatus);
}

void procManProcessSendSignal(ProcManPid pid, ByteCodeSignalId signalId) {
	// Check signal id
	if (signalId>ByteCodeSignalIdNB) {
		kernelLog(LogTypeWarning, "could not send signal %u to process %u, bad signal id\n", signalId, pid);
		return;
	}

	// Find process
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL) {
		kernelLog(LogTypeWarning, "could not send signal %u to process %u, no such process\n", signalId, pid);
		return;
	}

	// Load process' data.
	ProcManProcessProcData procData;
	if (!procManProcessLoadProcData(process, &procData)) {
		kernelLog(LogTypeWarning, "could not send signal %u to process %u, could not load process ddata\n", signalId, pid);
		return;
	}

	// Look for a registered handler.
	uint16_t handlerAddr=procData.signalHandlers[signalId];
	if (handlerAddr==0) {
		kernelLog(LogTypeInfo, "sent signal %u to process %u, but no handler registered so ignoring\n", signalId, pid);
		return;
	}

	// Inspect processes current state
	switch(process->state) {
		case ProcManProcessStateUnused:
			assert(false); // shouldn't reach here
		break;
		case ProcManProcessStateActive:
			// Nothing special to do - ready to accept signal
		break;
		case ProcManProcessStateWaitingWaitpid:
			// Set process active again but set r0 to indicate waitpid was interrupted.
			process->state=ProcManProcessStateActive;
			procData.regs[0]=ProcManExitStatusInterrupted;
		break;
		case ProcManProcessStateWaitingRead:
			// Set process active again but set r0 to indicate read failed
			process->state=ProcManProcessStateActive;
			procData.regs[0]=0;
		break;
	}
	assert(process->state==ProcManProcessStateActive);

	// 'Call' the registered handler (in the same way the assembler generates call instructions)
	// Do this by pushing the current IP as the return address, before jumping into handler.
	procManProcessMemoryWriteWord(process, &procData, procData.regs[ByteCodeRegisterSP], procData.regs[ByteCodeRegisterIP]); // TODO: Check return
	procData.regs[ByteCodeRegisterSP]+=2;
	procData.regs[ByteCodeRegisterIP]=handlerAddr;

	// Save process' data.
	if (!procManProcessStoreProcData(process, &procData)) {
		kernelLog(LogTypeInfo, "could not send signal %u to process %u, could not store process ddata\n", signalId, pid);
		return;
	}

	kernelLog(LogTypeInfo, "sent signal %u to process %u, calling registered handler at %u\n", signalId, pid, handlerAddr);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid) {
	if (procManData.processes[pid].progmemFd!=KernelFsFdInvalid)
		return procManData.processes+pid;
	return NULL;
}

ProcManPid procManGetPidFromProcess(const ProcManProcess *process) {
	return process-procManData.processes;
}

const char *procManGetExecPathFromProcess(const ProcManProcess *process) {
	return kernelFsGetFilePath(process->progmemFd);
}

ProcManPid procManFindUnusedPid(void) {
	// Given that fork uses return pid 0 to indicate child process, we have to make sure the first process created uses pid 0, and exists for as long as the system is running (so that fork can never return)
	for(int i=0; i<ProcManPidMax; ++i)
		if (procManData.processes[i].state==ProcManProcessStateUnused)
			return i;
	return ProcManPidMax;
}

bool procManProcessLoadProcData(const ProcManProcess *process, ProcManProcessProcData *procData) {
	return (kernelFsFileReadOffset(process->procFd, 0, (uint8_t *)procData, sizeof(ProcManProcessProcData), false)==sizeof(ProcManProcessProcData));
}

bool procManProcessStoreProcData(ProcManProcess *process, ProcManProcessProcData *procData) {
	return (kernelFsFileWriteOffset(process->procFd, 0, (const uint8_t *)procData, sizeof(ProcManProcessProcData))==sizeof(ProcManProcessProcData));
}

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, uint8_t *value) {
	if (addr<ByteCodeMemoryRamAddr) {
		// Addresss is in progmem data
		if (kernelFsFileReadOffset(process->progmemFd, addr, value, 1, false)==1)
			return true;
		else {
			kernelLog(LogTypeWarning, "process %u (%s) tried to read invalid address (0x%04X, pointing to PROGMEM at offset %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, addr);
			return false;
		}
	} else {
		// Address is in RAM
		ByteCodeWord ramIndex=(addr-ByteCodeMemoryRamAddr);
		if (ramIndex<procData->ramLen) {
			if (!kernelFsFileReadOffset(procData->ramFd, procData->argvDataLen+ramIndex, value, 1, false)) {
				kernelLog(LogTypeWarning, "process %u (%s) tried to read valid address (0x%04X, pointing to RAM at offset %u, size %u) but failed, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, procData->ramLen);
				return false;
			}
			return true;
		} else {
			kernelLog(LogTypeWarning, "process %u (%s) tried to read invalid address (0x%04X, pointing to RAM at offset %u, but size is only %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, procData->ramLen);
			return false;
		}
	}
}

bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, ByteCodeWord *value) {
	uint8_t upper, lower;
	if (!procManProcessMemoryReadByte(process, procData, addr, &upper))
		return false;
	if (!procManProcessMemoryReadByte(process, procData, addr+1, &lower))
		return false;
	*value=(((ByteCodeWord)upper)<<8)|((ByteCodeWord)lower);
	return true;
}

bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, char *str, uint16_t len) {
	while(len-->0) {
		uint8_t c;
		if (!procManProcessMemoryReadByte(process, procData, addr++, &c))
			return false;
		*str++=c;
		if (c=='\0')
			break;
	}

	return true;
}

bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, uint8_t value) {
	// Is this addr in read-only progmem section?
	if (addr<ByteCodeMemoryRamAddr) {
		kernelLog(LogTypeWarning, "process %u (%s) tried to write to read-only address (0x%04X), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr);
		return false;
	}

	// addr is in RAM
	ByteCodeWord ramIndex=(addr-ByteCodeMemoryRamAddr);
	if (ramIndex<procData->ramLen) {
		if (!kernelFsFileWriteOffset(procData->ramFd, procData->argvDataLen+ramIndex, &value, 1)) {
			kernelLog(LogTypeWarning, "process %u (%s) tried to write to valid RAM address (0x%04X, ram offset %u), but failed, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex);
			return false;
		}
		return true;
	} else {
		// Close ram file
		char ramFdPath[KernelFsPathMax];
		strcpy(ramFdPath, kernelFsGetFilePath(procData->ramFd));
		kernelFsFileClose(procData->ramFd);

		// Resize ram file (trying for up to 16 bytes extra, but falling back on minimum if fails)
		KernelFsFileOffset oldRamLen=procData->ramLen;
		uint16_t newRamLenMin=ramIndex+1;
		uint16_t newRamTotalSizeMin=procData->argvDataLen+newRamLenMin;

		uint16_t extra;
		uint16_t newRamLen;
		for(extra=16; extra>0; extra/=2) {
			// Compute new len and total size for this amount of extra bytes
			newRamLen=newRamLenMin+extra-1;
			uint16_t newRamTotalSize=newRamTotalSizeMin+extra-1;

			// Attempt to resize ram file
			if (kernelFsFileResize(ramFdPath, newRamTotalSize))
				break;

			// Unable to allocate even 1 extra byte?
			if (extra<=1) {
				// TODO: tidy up better in this case? (ramfd in particular)
				kernelLog(LogTypeWarning, "process %u (%s) tried to write to RAM address (0x%04X, ram offset %u), beyond current size, but we could not allocate new size needed (%u vs original %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, newRamLen, oldRamLen);
				return false;
			}
		}

		// Re-open ram file
		procData->ramFd=kernelFsFileOpen(ramFdPath);
		if (procData->ramFd==KernelFsFdInvalid) {
			// TODO: tidy up better in this case? (ramfd in particular)
			kernelLog(LogTypeWarning, "process %u (%s) tried to write to RAM address (0x%04X, ram offset %u), beyond current size, but we could not reopen ram file after resizing, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex);
			return false;
		}

		// Update stored ram len and write byte
		procData->ramLen=newRamLen;
		if (!kernelFsFileWriteOffset(procData->ramFd, procData->argvDataLen+ramIndex, &value, 1)) {
			kernelLog(LogTypeWarning, "process %u (%s) tried to write to RAM address (0x%04X, ram offset %u), beyond current size. We have resized (%u vs original %u) and reopened the file, but still could not write, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, newRamLen, oldRamLen);
			return false;
		}

		return true;
	}
}

bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, ByteCodeWord value) {
	if (!procManProcessMemoryWriteByte(process, procData, addr, (value>>8)))
		return false;
	if (!procManProcessMemoryWriteByte(process, procData, addr+1, (value&0xFF)))
		return false;
	return true;
}

bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, const char *str) {
	for(const char *c=str; ; ++c) {
		if (!procManProcessMemoryWriteByte(process, procData, addr++, *c))
			return false;
		if (*c=='\0')
			break;
	}
	return true;
}

bool procManProcessGetArgvN(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char str[64]) {
	char *dest=str;

	// Check n is sensible
	if (n>=ARGVMAX) {
		*dest='\0';
		return true;
	}

	// Grab argument
	uint16_t index=procData->envVars.argv[n];
	while(1) {
		uint8_t c;
		if (kernelFsFileReadOffset(procData->ramFd, index, &c, 1, false)!=1) {
			kernelLog(LogTypeWarning, "corrupt argvdata or ram file more generally? Process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			return false;
		}
		*dest++=c;
		if (c=='\0')
			break;
		++index;
	}

	return true;
}

bool procManProcessGetInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong *instruction) {
	if (!procManProcessMemoryReadByte(process, procData, procData->regs[ByteCodeRegisterIP]++, ((uint8_t *)instruction)+0))
		return false;
	BytecodeInstructionLength length=bytecodeInstructionParseLength(*instruction);
	if (length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)
		if (!procManProcessMemoryReadByte(process, procData, procData->regs[ByteCodeRegisterIP]++, ((uint8_t *)instruction)+1))
			return false;
	if (length==BytecodeInstructionLengthLong)
		if (!procManProcessMemoryReadByte(process, procData, procData->regs[ByteCodeRegisterIP]++, ((uint8_t *)instruction)+2))
			return false;
	return true;
}

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong instruction, ProcManExitStatus *exitStatus) {
	// Parse instruction
	BytecodeInstructionInfo info;
	if (!bytecodeInstructionParse(&info, instruction)) {
		kernelLog(LogTypeWarning, "could not parse instruction 0x%02X%02X%02X, process %u (%s), killing\n", instruction[0], instruction[1], instruction[2], procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return false;
	}

	// Execute instruction
	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore8:
					if (!procManProcessMemoryWriteByte(process, procData, procData->regs[info.d.memory.destReg], procData->regs[info.d.memory.srcReg])) {
						kernelLog(LogTypeWarning, "failed during store8 instruction execution, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
				break;
				case BytecodeInstructionMemoryTypeLoad8: {
					uint8_t value;
					if (!procManProcessMemoryReadByte(process, procData, procData->regs[info.d.memory.srcReg], &value)) {
						kernelLog(LogTypeWarning, "failed during load8 instruction execution, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					procData->regs[info.d.memory.destReg]=value;
				} break;
				case BytecodeInstructionMemoryTypeReserved: {
					kernelLog(LogTypeWarning, "reserved instrution, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				break;
			}
		break;
		case BytecodeInstructionTypeAlu: {
			int opA=procData->regs[info.d.alu.opAReg];
			int opB=procData->regs[info.d.alu.opBReg];
			switch(info.d.alu.type) {
				case BytecodeInstructionAluTypeInc:
					procData->regs[info.d.alu.destReg]+=info.d.alu.incDecValue;
				break;
				case BytecodeInstructionAluTypeDec:
					procData->regs[info.d.alu.destReg]-=info.d.alu.incDecValue;
				break;
				case BytecodeInstructionAluTypeAdd:
					procData->regs[info.d.alu.destReg]=opA+opB;
				break;
				case BytecodeInstructionAluTypeSub:
					procData->regs[info.d.alu.destReg]=opA-opB;
				break;
				case BytecodeInstructionAluTypeMul:
					procData->regs[info.d.alu.destReg]=opA*opB;
				break;
				case BytecodeInstructionAluTypeDiv:
					if (opB==0) {
						kernelLog(LogTypeWarning, "division by zero, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					procData->regs[info.d.alu.destReg]=opA/opB;
				break;
				case BytecodeInstructionAluTypeXor:
					procData->regs[info.d.alu.destReg]=opA^opB;
				break;
				case BytecodeInstructionAluTypeOr:
					procData->regs[info.d.alu.destReg]=opA|opB;
				break;
				case BytecodeInstructionAluTypeAnd:
					procData->regs[info.d.alu.destReg]=opA&opB;
				break;
				case BytecodeInstructionAluTypeNot:
					procData->regs[info.d.alu.destReg]=~opA;
				break;
				case BytecodeInstructionAluTypeCmp: {
					ByteCodeWord *d=&procData->regs[info.d.alu.destReg];
					*d=0;
					*d|=(opA==opB)<<BytecodeInstructionAluCmpBitEqual;
					*d|=(opA==0)<<BytecodeInstructionAluCmpBitEqualZero;
					*d|=(opA!=opB)<<BytecodeInstructionAluCmpBitNotEqual;
					*d|=(opA!=0)<<BytecodeInstructionAluCmpBitNotEqualZero;
					*d|=(opA<opB)<<BytecodeInstructionAluCmpBitLessThan;
					*d|=(opA<=opB)<<BytecodeInstructionAluCmpBitLessEqual;
					*d|=(opA>opB)<<BytecodeInstructionAluCmpBitGreaterThan;
					*d|=(opA>=opB)<<BytecodeInstructionAluCmpBitGreaterEqual;
				} break;
				case BytecodeInstructionAluTypeShiftLeft:
					procData->regs[info.d.alu.destReg]=opA<<opB;
				break;
				case BytecodeInstructionAluTypeShiftRight:
					procData->regs[info.d.alu.destReg]=opA>>opB;
				break;
				case BytecodeInstructionAluTypeSkip:
					procData->skipFlag=(procData->regs[info.d.alu.destReg] & (1u<<info.d.alu.opAReg));
				break;
				case BytecodeInstructionAluTypeStore16: {
					ByteCodeWord destAddr=procData->regs[info.d.alu.destReg];
					if (!procManProcessMemoryWriteWord(process, procData, destAddr, opA)) {
						kernelLog(LogTypeWarning, "failed during store16 instruction execution, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
				} break;
				case BytecodeInstructionAluTypeLoad16: {
					ByteCodeWord srcAddr=procData->regs[info.d.alu.opAReg];
					if (!procManProcessMemoryReadWord(process, procData, srcAddr, &procData->regs[info.d.alu.destReg])) {
						kernelLog(LogTypeWarning, "failed during load16 instruction execution, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
				} break;
			}
		} break;
		case BytecodeInstructionTypeMisc:
			switch(info.d.misc.type) {
				case BytecodeInstructionMiscTypeNop:
				break;
				case BytecodeInstructionMiscTypeSyscall: {
					uint16_t syscallId=procData->regs[0];
					switch(syscallId) {
						case ByteCodeSyscallIdExit:
							*exitStatus=procData->regs[1];
							kernelLog(LogTypeWarning, "exit syscall from process %u (%s), status %u, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), *exitStatus);
							return false;
						break;
						case ByteCodeSyscallIdGetPid:
							procData->regs[0]=procManGetPidFromProcess(process);
						break;
						case ByteCodeSyscallIdGetArgC: {
							procData->regs[0]=0;
							for(unsigned i=0; i<ARGVMAX; ++i) {
								char arg[64]; // TODO: Avoid hardcoded limit
								if (!procManProcessGetArgvN(process, procData, i, arg)) {
									kernelLog(LogTypeWarning, "failed during getargc syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]+=(strlen(arg)>0);
							}
						} break;
						case ByteCodeSyscallIdGetArgVN: {
							int n=procData->regs[1];
							ByteCodeWord bufAddr=procData->regs[2];
							// TODO: Use this: ByteCodeWord bufLen=procData->regs[3];

							if (n>ARGVMAX)
								procData->regs[0]=0;
							else {
								char arg[64]; // TODO: Avoid hardcoded limit
								if (!procManProcessGetArgvN(process, procData, n, arg)) {
									kernelLog(LogTypeWarning, "failed during getargvn syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, arg)) {
									kernelLog(LogTypeWarning, "failed during getargvn syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]=strlen(arg);
							}
						} break;
						case ByteCodeSyscallIdFork:
							procManProcessFork(process, procData);
						break;
						case ByteCodeSyscallIdExec:
							if (!procManProcessExec(process, procData)) {
								kernelLog(LogTypeWarning, "failed during exec syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
						break;
						case ByteCodeSyscallIdWaitPid: {
							ByteCodeWord waitPid=procData->regs[1];
							ByteCodeWord timeout=procData->regs[2];

							// If given pid does not represent a process, return immediately
							if (procManGetProcessByPid(waitPid)==NULL) {
								procData->regs[0]=ProcManExitStatusNoProcess;
							} else {
								// Otherwise indicate process is waiting for this pid to die
								process->state=ProcManProcessStateWaitingWaitpid;
								process->waitingData8=waitPid;
								process->waitingData16=(timeout>0 ? (millis()+999)/1000+timeout : 0); // +999 is to make sure we do not sleep for less than the given number of seconds (as we would if we round the result of millis down)
							}
						} break;
						case ByteCodeSyscallIdGetPidPath: {
							ProcManPid pid=procData->regs[1];
							ByteCodeWord bufAddr=procData->regs[2];

							ProcManProcess *qProcess=procManGetProcessByPid(pid);
							if (qProcess!=NULL) {
								const char *execPath=procManGetExecPathFromProcess(qProcess);
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, execPath)) {
									kernelLog(LogTypeWarning, "failed during getpidpath syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]=1;
							} else
								procData->regs[0]=0;
						} break;
						case ByteCodeSyscallIdGetPidState: {
							ProcManPid pid=procData->regs[1];
							ByteCodeWord bufAddr=procData->regs[2];

							ProcManProcess *qProcess=procManGetProcessByPid(pid);
							if (qProcess!=NULL) {
								const char *str="???";
								switch(qProcess->state) {
									case ProcManProcessStateUnused:
										str="unused";
									break;
									case ProcManProcessStateActive:
										str="active";
									break;
									case ProcManProcessStateWaitingWaitpid:
										str="waiting";
									break;
									case ProcManProcessStateWaitingRead:
										str="waiting";
									break;
								}
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, str)) {
									kernelLog(LogTypeWarning, "failed during getpidstate syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]=1;
							} else
								procData->regs[0]=0;
						} break;
						case ByteCodeSyscallIdGetAllCpuCounts: {
							ByteCodeWord bufAddr=procData->regs[1];
							for(unsigned i=0; i<ProcManPidMax; ++i) {
								ProcManProcess *qProcess=procManGetProcessByPid(i);
								uint16_t value;
								if (qProcess!=NULL)
									value=qProcess->instructionCounter;
								else
									value=0;
								if (!procManProcessMemoryWriteWord(process, procData, bufAddr, value)) {
									kernelLog(LogTypeWarning, "failed during getallcpucounts syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								bufAddr+=2;
							}
						} break;
						case ByteCodeSyscallIdKill: {
							ProcManPid pid=procData->regs[1];
							if (pid!=0) // do not allow killing init
								procManProcessKill(pid, ProcManExitStatusKilled);
						} break;
						case ByteCodeSyscallIdGetPidRam: {
							ByteCodeWord pid=procData->regs[1];
							ProcManProcess *qProcess=procManGetProcessByPid(pid);
							if (qProcess!=NULL) {
								ProcManProcessProcData qProcData;
								if (!procManProcessLoadProcData(qProcess, &qProcData)) {
									kernelLog(LogTypeWarning, "process %u getpid %u - could not load q proc data\n", pid, procManGetPidFromProcess(qProcess));
									procData->regs[0]=0;
								} else
									procData->regs[0]=sizeof(ProcManProcessProcData)+qProcData.argvDataLen+qProcData.ramLen;
							} else
								procData->regs[0]=0;
						} break;
						case ByteCodeSyscallIdRead: {
							KernelFsFd fd=procData->regs[1];

							if (!kernelFsFileCanRead(fd)) {
								// Reading would block - so enter waiting state until data becomes available.
								process->state=ProcManProcessStateWaitingRead;
								process->waitingData8=fd;
							} else {
								// Otherwise read as normal (stopping before we would block)
								if (!procManProcessRead(process, procData)) {
									kernelLog(LogTypeWarning, "failed during read syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
							}
						} break;
						case ByteCodeSyscallIdWrite: {
							KernelFsFd fd=procData->regs[1];
							uint16_t offset=procData->regs[2];
							uint16_t bufAddr=procData->regs[3];
							KernelFsFileOffset len=procData->regs[4];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value;
								if (!procManProcessMemoryReadByte(process, procData, bufAddr+i, &value)) {
									kernelLog(LogTypeWarning, "failed during write syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								if (kernelFsFileWriteOffset(fd, offset+i, &value, 1)!=1)
									break;
							}
							procData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdOpen: {
							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during open syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(path);
							procData->regs[0]=kernelFsFileOpen(path);

							#ifndef ARDUINO
							if (procData->regs[0]!=KernelFsFdInvalid && strcmp(path, "/dev/ttyS0")==0) {
								assert(kernelReaderPid==ProcManPidMax);
								kernelReaderPid=procManGetPidFromProcess(process);
							}
							#endif
						} break;
						case ByteCodeSyscallIdClose:
							#ifndef ARDUINO
							if (strcmp(kernelFsGetFilePath(procData->regs[1]), "/dev/ttyS0")==0) {
								assert(kernelReaderPid==procManGetPidFromProcess(process));
								kernelReaderPid=ProcManPidMax;
							}
							#endif
							kernelFsFileClose(procData->regs[1]);
						break;
						case ByteCodeSyscallIdDirGetChildN: {
							KernelFsFd fd=procData->regs[1];
							ByteCodeWord childNum=procData->regs[2];
							uint16_t bufAddr=procData->regs[3];

							char childPath[KernelFsPathMax];
							bool result=kernelFsDirectoryGetChild(fd, childNum, childPath);

							if (result) {
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, childPath)) {
									kernelLog(LogTypeWarning, "failed during dirgetchildn syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]=1;
							} else {
								procData->regs[0]=0;
							}
						} break;
						case ByteCodeSyscallIdGetPath: {
							KernelFsFd fd=procData->regs[1];
							uint16_t bufAddr=procData->regs[2];

							const char *srcPath=kernelFsGetFilePath(fd);
							if (srcPath==NULL)
								procData->regs[0]=0;
							else {
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, srcPath)) {
									kernelLog(LogTypeWarning, "failed during getpath syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]=1;
							}
						} break;
						case ByteCodeSyscallIdResizeFile: {
							// Grab path and new size
							uint16_t pathAddr=procData->regs[1];
							KernelFsFileOffset newSize=procData->regs[2];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
									kernelLog(LogTypeWarning, "failed during resizefile syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(path);

							// Resize (or create if does not exist)
							if (kernelFsFileExists(path))
								procData->regs[0]=kernelFsFileResize(path, newSize);
							else
								procData->regs[0]=kernelFsFileCreateWithSize(path, newSize);
						} break;
						case ByteCodeSyscallIdFileGetLen: {
							uint16_t pathAddr=procData->regs[1];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during filegetlen syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(path);
							procData->regs[0]=(kernelFsPathIsValid(path) ? kernelFsFileGetLen(path) : 0);
						} break;
						case ByteCodeSyscallIdTryReadByte: {
							KernelFsFd fd=procData->regs[1];

							// save terminal settings
							static struct termios termOld, termNew;
							tcgetattr(STDIN_FILENO, &termOld);

							// change terminal settings to avoid waiting for a newline before getting data
							termNew=termOld;
							termNew.c_lflag&=~ICANON;
							tcsetattr(STDIN_FILENO, TCSANOW, &termNew);

							// attempt to read
							uint8_t value;
							KernelFsFileOffset readResult=kernelFsFileReadOffset(fd, 0, &value, 1, false);

							// restore terminal settings
							tcsetattr(STDIN_FILENO, TCSANOW, &termOld);

							// set result in r0
							if (readResult==1)
								procData->regs[0]=value;
							else
								procData->regs[0]=256;
						} break;
						case ByteCodeSyscallIdIsDir: {
							uint16_t pathAddr=procData->regs[1];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during isdir syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(path);
							procData->regs[0]=kernelFsFileIsDir(path);
						} break;
						case ByteCodeSyscallIdFileExists: {
							uint16_t pathAddr=procData->regs[1];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during fileexists syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(path);
							procData->regs[0]=(kernelFsPathIsValid(path) ? kernelFsFileExists(path) : false);
						} break;
						case ByteCodeSyscallIdDelete: {
							uint16_t pathAddr=procData->regs[1];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during delete syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(path);
							procData->regs[0]=(kernelFsPathIsValid(path) ? kernelFsFileDelete(path) : false);
						} break;
						case ByteCodeSyscallIdEnvGetStdioFd:
							procData->regs[0]=procData->envVars.stdioFd;
						break;
						case ByteCodeSyscallIdEnvSetStdioFd:
							procData->envVars.stdioFd=procData->regs[1];
						break;
						case ByteCodeSyscallIdEnvGetPwd:
							if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], procData->envVars.pwd)) {
								kernelLog(LogTypeWarning, "failed during envgetpwd syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
						break;
						case ByteCodeSyscallIdEnvSetPwd:
							if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], procData->envVars.pwd, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during envsetpwd syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(procData->envVars.pwd);
						break;
						case ByteCodeSyscallIdEnvGetPath:
							if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], procData->envVars.path)) {
								kernelLog(LogTypeWarning, "failed during envgetpath syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
						break;
						case ByteCodeSyscallIdEnvSetPath:
							if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], procData->envVars.path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during envsetpath syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(procData->envVars.path);
						break;
						case ByteCodeSyscallIdTimeMonotonic:
							procData->regs[0]=(millis()/1000);
						break;
						case ByteCodeSyscallIdRegisterSignalHandler: {
							uint16_t signalId=procData->regs[1];
							uint16_t handlerAddr=procData->regs[2];

							if (signalId>=ByteCodeSignalIdNB) {
								kernelLog(LogTypeWarning, "process %u (%s), tried to register handler for invalid signal %u\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), signalId);
							} else
								procData->signalHandlers[signalId]=handlerAddr;

						} break;
						default:
							kernelLog(LogTypeWarning, "invalid syscall id=%i, process %u (%s), killing\n", syscallId, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
							return false;
						break;
					}
				} break;
				case BytecodeInstructionMiscTypeSet8:
					procData->regs[info.d.misc.d.set8.destReg]=info.d.misc.d.set8.value;
				break;
				case BytecodeInstructionMiscTypeSet16:
					procData->regs[info.d.misc.d.set16.destReg]=info.d.misc.d.set16.value;
				break;
			}
		break;
	}

	return true;
}

void procManProcessFork(ProcManProcess *parent, ProcManProcessProcData *procData) {
	KernelFsFd ramFd=KernelFsFdInvalid;
	ProcManPid parentPid=procManGetPidFromProcess(parent);

	kernelLog(LogTypeInfo, "fork request from process %u\n", parentPid);

	// Find a PID for the new process
	ProcManPid childPid=procManFindUnusedPid();
	if (childPid==ProcManPidMax) {
		kernelLog(LogTypeWarning, "could not fork from %u - no spare PIDs\n", parentPid);
		goto error;
	}
	ProcManProcess *child=&(procManData.processes[childPid]);

	// Construct proc file path
	// TODO: Try others if exists
	char childProcPath[KernelFsPathMax], childRamPath[KernelFsPathMax];
	sprintf(childProcPath, "/tmp/proc%u", childPid);
	sprintf(childRamPath, "/tmp/ram%u", childPid);

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(childProcPath, sizeof(ProcManProcessProcData))) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not create child process data file at '%s' of size %u\n", parentPid, childProcPath, sizeof(procManProcessLoadProcData));
		goto error;
	}
	uint16_t ramTotalSize=procData->argvDataLen+procData->ramLen;
	if (!kernelFsFileCreateWithSize(childRamPath, ramTotalSize)) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not create child ram data file at '%s' of size %u\n", parentPid, childRamPath, ramTotalSize);
		goto error;
	}

	// Attempt to open proc and ram files
	child->procFd=kernelFsFileOpen(childProcPath);
	if (child->procFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not open child process data file at '%s'\n", parentPid, childProcPath);
		goto error;
	}

	ramFd=kernelFsFileOpen(childRamPath);
	if (ramFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not open child ram data file at '%s'\n", parentPid, childRamPath);
		goto error;
	}

	// Simply use same FD as parent for the program data
	child->state=ProcManProcessStateActive;
	child->progmemFd=procManData.processes[parentPid].progmemFd;
	child->instructionCounter=0;

	// Initialise proc file
	ProcManProcessProcData childProcData=*procData;
	childProcData.ramFd=ramFd;
	childProcData.regs[0]=0; // indicate success in the child

	if (!procManProcessStoreProcData(child, &childProcData)) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not save child process data file to '%s'\n", parentPid, childProcPath);
		goto error;
	}

	// Copy parent's ram into child's
	for(KernelFsFileOffset i=0; i<ramTotalSize; ++i) {
		bool res=true;
		uint8_t value;
		res&=(kernelFsFileReadOffset(procData->ramFd, i, &value, 1, false)==1);
		res&=(kernelFsFileWriteOffset(childProcData.ramFd, i, &value, 1)==1);
		if (!res) {
			kernelLog(LogTypeWarning, "could not fork from %u - could not copy parent's RAM into child's (managed %u/%u)\n", parentPid, i, ramTotalSize);
			goto error;
		}
	}

	// Update parent return value with child's PID
	procData->regs[0]=childPid;

	kernelLog(LogTypeInfo, "forked from %u, creating child %u\n", parentPid, childPid);

	return;

	error:
	if (childPid!=ProcManPidMax) {
		procManData.processes[childPid].progmemFd=KernelFsFdInvalid;
		kernelFsFileClose(procManData.processes[childPid].procFd);
		procManData.processes[childPid].procFd=KernelFsFdInvalid;
		kernelFsFileDelete(childProcPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
		kernelFsFileClose(ramFd);
		kernelFsFileDelete(childRamPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
		procManData.processes[childPid].state=ProcManProcessStateUnused;
		procManData.processes[childPid].instructionCounter=0;
	}

	// Indicate error
	procData->regs[0]=ProcManPidMax;
}

bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData) {
	kernelLog(LogTypeInfo, "exec in %u\n", procManGetPidFromProcess(process));

	// Grab path and args (if any)
	char args[ARGVMAX][64]; // TODO: Avoid hardcoded 64
	for(unsigned i=0; i<ARGVMAX; ++i)
		args[i][0]='\0';

	if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], args[0], KernelFsPathMax)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not read path argument\n", procManGetPidFromProcess(process));
		return false;
	}

	for(unsigned i=1; i<ARGVMAX; ++i)
		if (procData->regs[i+1]!=0)
			if (!procManProcessMemoryReadStr(process, procData, procData->regs[i+1], args[i], KernelFsPathMax)) {
				kernelLog(LogTypeWarning, "exec in %u failed - could not read argument %u\n", procManGetPidFromProcess(process), i);
				return false;
			}

	kernelLog(LogTypeInfo, "exec in %u - raw path '%s', arg1='%s', arg2='%s', arg3='%s'\n", procManGetPidFromProcess(process), args[0], args[1], args[2], args[3]); // TODO: Avoid hardcoded number of arguments

	// Normalise path and then check if valid
	kernelFsPathNormalise(args[0]);

	if (!kernelFsPathIsValid(args[0])) {
		kernelLog(LogTypeWarning, "exec in %u failed - path '%s' not valid\n", procManGetPidFromProcess(process), args[0]);
		return true;
	}

	if (kernelFsFileIsDir(args[0])) {
		kernelLog(LogTypeWarning, "exec in %u failed - path '%s' is a directory\n", procManGetPidFromProcess(process), args[0]);
		return true;
	}

	// Attempt to open new program file
	KernelFsFd newProgmemFd=kernelFsFileOpen(args[0]);
	if (newProgmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not open program at '%s'\n", procManGetPidFromProcess(process), args[0]);
		return true;
	}

	// Close old fd (if not shared)
	ProcManPid pid=procManGetPidFromProcess(process);
	KernelFsFd oldProgmemFd=procManData.processes[pid].progmemFd;
	procManData.processes[pid].progmemFd=newProgmemFd;

	unsigned i;
	for(i=0; i<ProcManPidMax; ++i)
		if (procManData.processes[i].progmemFd==oldProgmemFd)
			break;
	if (i==ProcManPidMax)
		kernelFsFileClose(oldProgmemFd);

	// Reset instruction pointer
	procData->regs[ByteCodeRegisterIP]=0;

	// Update argv array and calculate argVDataLen
	procData->argvDataLen=0;
	for(unsigned i=0; i<ARGVMAX; ++i) {
		uint16_t argLen=strlen(args[i]);
		procData->envVars.argv[i]=procData->argvDataLen;
		procData->argvDataLen+=argLen+1; // +1 for null terminator
	}

	// Resize ram file (clear data and add new arguments)
	procData->ramLen=0; // no ram needed initially
	uint16_t newRamTotalSize=procData->argvDataLen+procData->ramLen;

	char ramPath[KernelFsPathMax];
	sprintf(ramPath, "/tmp/ram%u", pid);

	kernelFsFileClose(procData->ramFd);
	if (!kernelFsFileResize(ramPath, newRamTotalSize)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not resize new processes RAM file at '%s' to %u\n", procManGetPidFromProcess(process), ramPath, newRamTotalSize);
		return false;
	}
	procData->ramFd=kernelFsFileOpen(ramPath);
	assert(procData->ramFd!=KernelFsFdInvalid);

	// Write args into ram file
	for(unsigned i=0; i<ARGVMAX; ++i) {
		uint16_t argSize=strlen(args[i])+1;
		if (kernelFsFileWriteOffset(procData->ramFd, procData->envVars.argv[i], (const uint8_t *)(args[i]), argSize)!=argSize) {
			kernelLog(LogTypeWarning, "exec in %u failed - could not write args into new processes memory\n", procManGetPidFromProcess(process));
			return false;
		}
	}

	kernelLog(LogTypeInfo, "exec in %u succeeded\n", procManGetPidFromProcess(process));

	return true;
}

bool procManProcessRead(ProcManProcess *process, ProcManProcessProcData *procData) {
	KernelFsFd fd=procData->regs[1];
	uint16_t offset=procData->regs[2];
	uint16_t bufAddr=procData->regs[3];
	KernelFsFileOffset len=procData->regs[4];

	KernelFsFileOffset i;
	for(i=0; i<len; ++i) {
		uint8_t value;
		if (kernelFsFileReadOffset(fd, offset+i, &value, 1, i==0)!=1)
			break;
		if (!procManProcessMemoryWriteByte(process, procData, bufAddr+i, value))
			return false;
	}
	procData->regs[0]=i;

	return true;
}

void procManResetInstructionCounters(void) {
	for(unsigned i=0; i<ProcManPidMax; ++i)
		procManData.processes[i].instructionCounter=0;
}
