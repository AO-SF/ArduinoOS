#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef ARDUINO
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "kernel.h"
#include "kernelfs.h"
#include "kernelmount.h"
#include "log.h"
#include "procman.h"
#include "wrapper.h"

#define procManProcessInstructionCounterMax (65536llu) // TODO: On arduino this needs 32 bit
#define procManProcessInstructionCounterMaxMinusOne (65535lu)
#define procManProcessTickInstructionsPerTick 80 // Generally a higher value causes faster execution, but decreased responsiveness if many processes running
#define procManTicksPerInstructionCounterReset (800) // must not exceed procManProcessInstructionCounterMax/procManProcessTickInstructionsPerTick, which is currently ~819. target is to reset roughly every 10s

#define ProcManSignalHandlerInvalid 0

#define ProcManArgLenMax 32
#define ProcManEnvVarPathMax 64

typedef enum {
	ProcManProcessStateUnused,
	ProcManProcessStateActive,
	ProcManProcessStateWaitingWaitpid,
	ProcManProcessStateWaitingRead,
	ProcManProcessStateExiting,
} ProcManProcessState;

#define ARGVMAX 4

typedef struct {
	// Process data
	ByteCodeWord regs[BytecodeRegisterNB];
	uint8_t signalHandlers[ByteCodeSignalIdNB]; // pointers to functions to run on signals (restricted to first 256 bytes)
	uint16_t ramLen;
	uint8_t envVarDataLen;
	uint8_t ramFd;

	// Environment variables
	KernelFsFd stdioFd; // set to KernelFsFdInvalid when init is called

	// The following fields are pointers into the start of the ramFd file.
	// The arguments are fixed, but pwd and path may be updated later by the process itself to point to new strings, beyond the initial read-only section (and so need a full 16 bit offset).
	uint8_t argv[ARGVMAX];
	uint16_t pwd; // set to '/' when init is called
	uint16_t path; // set to '/usr/bin:/bin:' when init is called
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
bool procManProcessMemoryReadByteAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord offset, uint8_t *value);
bool procManProcessMemoryReadWordAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord offset, ByteCodeWord *value);
bool procManProcessMemoryReadStrAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord offset, char *str, uint16_t len);
bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, uint8_t value);
bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, ByteCodeWord value);
bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord addr, const char *str);

bool procManProcessGetArgvN(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char str[ProcManArgLenMax]); // Returns false to indicate illegal memory operation. Always succeeds otherwise, but str may be 0 length.  TODO: Avoid hardcoded limit

bool procManProcessGetInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong *instruction);
bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong instruction, ProcManExitStatus *exitStatus);

void procManProcessFork(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData); // Returns false only on critical error (e.g. segfault), i.e. may return true even though exec operation itself failed

KernelFsFd procManProcessLoadProgmemFile(ProcManProcess *process, char args[ARGVMAX][ProcManArgLenMax]); // Loads executable tiles, reading the magic byte (and potentially recursing), before returning fd of final executable (or KernelFsInvalid on failure)

bool procManProcessRead(ProcManProcess *process, ProcManProcessProcData *procData);

void procManResetInstructionCounters(void);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void) {
	// Clear processes table
	for(ProcManPid i=0; i<ProcManPidMax; ++i) {
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
	procManKillAll();
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

ProcManPid procManGetProcessCount(void) {
	ProcManPid count=0;
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		count+=(procManGetProcessByPid(pid)!=NULL);
	return count;
}

ProcManPid procManProcessNew(const char *programPath) {
	KernelFsFd ramFd=KernelFsFdInvalid;
	ProcManProcessProcData procData;

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

	// Load program (handling magic bytes)
	char args[ARGVMAX][ProcManArgLenMax];
	strcpy(args[0], programPath);
	for(uint8_t i=1; i<ARGVMAX; ++i)
		strcpy(args[i], "");

	procManData.processes[pid].progmemFd=procManProcessLoadProgmemFile(&procManData.processes[pid], args);
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not create new process - could not open progmem file ('%s')\n", args[0]);
		goto error;
	}

	// Create env vars data
	char envVarData[256]; // TODO: Should be larger for worst-case (roughly (argvmax+2)*pathmax~=380)
	uint8_t envVarDataLen=0;

	char tempPwd[KernelFsPathMax];
	strcpy(tempPwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(tempPwd, &dirname, &basename);
	assert(dirname==tempPwd);
	procData.pwd=envVarDataLen;
	strcpy(envVarData+envVarDataLen, tempPwd);
	envVarDataLen+=strlen(tempPwd)+1;

	char tempPath[ProcManEnvVarPathMax];
	strcpy(tempPath, "/usr/bin:/bin:");
	procData.path=envVarDataLen;
	strcpy(envVarData+envVarDataLen, tempPath);
	envVarDataLen+=strlen(tempPath)+1;

	for(uint8_t i=0; i<ARGVMAX; ++i) {
		procData.argv[i]=envVarDataLen;
		strcpy(envVarData+envVarDataLen, args[i]);
		envVarDataLen+=strlen(args[i])+1;
	}

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(procPath, sizeof(ProcManProcessProcData))) {
		kernelLog(LogTypeWarning, "could not create new process - could not create process data file at '%s' of size %u\n", procPath, sizeof(ProcManProcessProcData));
		goto error;
	}
	if (!kernelFsFileCreateWithSize(ramPath, envVarDataLen)) {
		kernelLog(LogTypeWarning, "could not create new process - could not create ram data file at '%s' of size %u\n", ramPath, envVarDataLen);
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

	// Initialise proc file (and env var data in ram file)
	procData.regs[ByteCodeRegisterIP]=0;
	for(uint16_t i=0; i<ByteCodeSignalIdNB; ++i)
		procData.signalHandlers[i]=ProcManSignalHandlerInvalid;
	procData.stdioFd=KernelFsFdInvalid;
	procData.envVarDataLen=envVarDataLen;
	procData.ramLen=0;
	procData.ramFd=ramFd;

	if (kernelFsFileWriteOffset(ramFd, 0, (uint8_t *)envVarData, envVarDataLen)!=envVarDataLen) {
		kernelLog(LogTypeWarning, "could not create new process - could not write env var data into ram file '%s', fd %u (tried %u bytes)\n", ramPath, ramFd, envVarDataLen);
		goto error;
	}

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

void procManKillAll(void) {
	for(ProcManPid i=0; i<ProcManPidMax; ++i)
		if (procManGetProcessByPid(i)!=NULL)
			procManProcessKill(i, ProcManExitStatusKilled);
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
		if (procManProcessLoadProcData(process, &procData) && procData.ramFd!=KernelFsFdInvalid) {
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
	for(ProcManPid waiterPid=0; waiterPid<ProcManPidMax; ++waiterPid) {
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
		case ProcManProcessStateExiting:
			// This shouldn't happen as exiting processes are removed as soon as the syscall runs.
			// But to be safe, kill
			kernelLog(LogTypeWarning, "process %u tick - internal error: state is exiting at tick init, killing\n", pid);
			goto kill;
		break;
	}

	// Run a few instructions
	for(uint16_t instructionNum=0; instructionNum<procManProcessTickInstructionsPerTick; ++instructionNum) {
		// Run a single instruction
		BytecodeInstructionLong instruction;
		if (!procManProcessGetInstruction(process, &procData, &instruction)) {
			kernelLog(LogTypeWarning, "process %u tick - could not get instruction, killing\n", pid);
			goto kill;
		}

		// Execute instruction
		if (!procManProcessExecInstruction(process, &procData, instruction, &exitStatus)) {
			if (process->state!=ProcManProcessStateExiting)
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
		case ProcManProcessStateExiting:
			// Shouldn't really happen, log
			kernelLog(LogTypeWarning, "sent signal %u to process %u, but its state is exiting (internal error), ignoring\n", signalId, pid);
			return;
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

bool procManProcessExists(ProcManPid pid) {
	return (procManGetProcessByPid(pid)!=NULL);
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
	for(ProcManPid i=0; i<ProcManPidMax; ++i)
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
		KernelFsFileOffset ramOffset=ramIndex+procData->envVarDataLen;
		return procManProcessMemoryReadByteAtRamfileOffset(process, procData, ramOffset, value);
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

bool procManProcessMemoryReadByteAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord offset, uint8_t *value) {
	KernelFsFileOffset ramTotalSize=procData->envVarDataLen+procData->ramLen;
	if (offset<ramTotalSize) {
		if (!kernelFsFileReadOffset(procData->ramFd, offset, value, 1, false)) {
			kernelLog(LogTypeWarning, "process %u (%s) tried to read valid address (RAM file offset %u) but failed, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), offset);
			return false;
		}
		return true;
	} else {
		kernelLog(LogTypeWarning, "process %u (%s) tried to read invalid address (RAM file offset %u, but size is only %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), offset, ramTotalSize);
		return false;
	}
}

bool procManProcessMemoryReadWordAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord offset, ByteCodeWord *value) {
	uint8_t upper, lower;
	if (!procManProcessMemoryReadByteAtRamfileOffset(process, procData, offset, &upper))
		return false;
	if (!procManProcessMemoryReadByteAtRamfileOffset(process, procData, offset+1, &lower))
		return false;
	*value=(((ByteCodeWord)upper)<<8)|((ByteCodeWord)lower);
	return true;
}

bool procManProcessMemoryReadStrAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, ByteCodeWord offset, char *str, uint16_t len) {
	while(len-->0) {
		uint8_t c;
		if (!procManProcessMemoryReadByteAtRamfileOffset(process, procData, offset++, &c))
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
		if (!kernelFsFileWriteOffset(procData->ramFd, procData->envVarDataLen+ramIndex, &value, 1)) {
			kernelLog(LogTypeWarning, "process %u (%s) tried to write to valid RAM address (0x%04X, ram offset %u), but failed, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex);
			return false;
		}
		return true;
	} else {
		// Close ram file
		char ramFdPath[KernelFsPathMax];
		strcpy(ramFdPath, kernelFsGetFilePath(procData->ramFd));
		kernelFsFileClose(procData->ramFd);
		procData->ramFd=KernelFsFdInvalid;

		// Resize ram file (trying for up to 16 bytes extra, but falling back on minimum if fails)
		KernelFsFileOffset oldRamLen=procData->ramLen;
		uint16_t newRamLenMin=ramIndex+1;
		uint16_t newRamTotalSizeMin=procData->envVarDataLen+newRamLenMin;

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
				kernelLog(LogTypeWarning, "process %u (%s) tried to write to RAM (0x%04X, offset %u), beyond size, but could not allocate new size (%u vs %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, newRamLen, oldRamLen);
				kernelFsFileDelete(ramFdPath);
				goto error;
			}
		}

		// Re-open ram file
		procData->ramFd=kernelFsFileOpen(ramFdPath);
		if (procData->ramFd==KernelFsFdInvalid) {
			kernelLog(LogTypeWarning, "process %u (%s) tried to write to RAM (0x%04X, offset %u), beyond size, but could not reopen file after resizing, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex);
			kernelFsFileDelete(ramFdPath);
			goto error;
		}

		// Update stored ram len and write byte
		procData->ramLen=newRamLen;
		if (!kernelFsFileWriteOffset(procData->ramFd, procData->envVarDataLen+ramIndex, &value, 1)) {
			kernelLog(LogTypeWarning, "process %u (%s) tried to write to RAM (0x%04X, offset %u) had to resize (%u vs %u), but could not write, killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, newRamLen, oldRamLen);
			goto error;
		}

		return true;

		error:
		// Store procdata back as otherwise when we come to kill ramFd will not be saved
		procManProcessStoreProcData(process, procData); // TODO: Check return
		return false;
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

bool procManProcessGetArgvN(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char str[ProcManArgLenMax]) {
	char *dest=str;

	// Check n is sensible
	if (n>=ARGVMAX) {
		*dest='\0';
		return true;
	}

	// Grab argument
	uint16_t index=procData->argv[n];
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
			ByteCodeWord opA=procData->regs[info.d.alu.opAReg];
			ByteCodeWord opB=procData->regs[info.d.alu.opBReg];
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
				case BytecodeInstructionAluTypeSkip: {
					if (procData->regs[info.d.alu.destReg] & (1u<<info.d.alu.opAReg)) {
						// Skip next instruction
						BytecodeInstructionLong nextInstruction;
						if (!procManProcessGetInstruction(process, procData, &nextInstruction)) {
							kernelLog(LogTypeWarning, "could not skip next instruction, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
							return false;
						}
					}
				} break;
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
							kernelLog(LogTypeInfo, "exit syscall from process %u (%s), status %u, updating state\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), *exitStatus);
							process->state=ProcManProcessStateExiting;
							return false;
						break;
						case ByteCodeSyscallIdGetPid:
							procData->regs[0]=procManGetPidFromProcess(process);
						break;
						case ByteCodeSyscallIdGetArgC: {
							procData->regs[0]=0;
							for(uint8_t i=0; i<ARGVMAX; ++i) {
								char arg[ProcManArgLenMax];
								if (!procManProcessGetArgvN(process, procData, i, arg)) {
									kernelLog(LogTypeWarning, "failed during getargc syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									return false;
								}
								procData->regs[0]+=(strlen(arg)>0);
							}
						} break;
						case ByteCodeSyscallIdGetArgVN: {
							uint8_t n=procData->regs[1];
							ByteCodeWord bufAddr=procData->regs[2];
							// TODO: Use this: ByteCodeWord bufLen=procData->regs[3];

							if (n>ARGVMAX)
								procData->regs[0]=0;
							else {
								char arg[ProcManArgLenMax];
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
									case ProcManProcessStateWaitingRead:
										str="waiting";
									break;
									case ProcManProcessStateExiting:
										str="exiting";
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
							for(ProcManPid i=0; i<ProcManPidMax; ++i) {
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
						case ByteCodeSyscallIdSignal: {
							ProcManPid targetPid=procData->regs[1];
							ByteCodeSignalId signalId=procData->regs[2];
							if (signalId<ByteCodeSignalIdNB) {
								if (targetPid==0 && signalId==ByteCodeSignalIdSuicide)
									kernelLog(LogTypeWarning, "process %u - warning cannot send signal 'suicide' to init (target pid=%u)\n", procManGetPidFromProcess(process), targetPid);
								else
									procManProcessSendSignal(targetPid, signalId);
							} else
								kernelLog(LogTypeWarning, "process %u - warning bad signalId %u in signal syscall (target pid=%u)\n", procManGetPidFromProcess(process), signalId, targetPid);
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
									procData->regs[0]=sizeof(ProcManProcessProcData)+qProcData.envVarDataLen+qProcData.ramLen;
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

							if (procData->regs[0]!=KernelFsFdInvalid && strcmp(path, "/dev/ttyS0")==0) {
								assert(kernelReaderPid==ProcManPidMax);
								kernelReaderPid=procManGetPidFromProcess(process);
							}
						} break;
						case ByteCodeSyscallIdClose:
							if (strcmp(kernelFsGetFilePath(procData->regs[1]), "/dev/ttyS0")==0) {
								assert(kernelReaderPid==procManGetPidFromProcess(process));
								kernelReaderPid=ProcManPidMax;
							}
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

							#ifndef ARDUINO
							// save terminal settings
							static struct termios termOld, termNew;
							tcgetattr(STDIN_FILENO, &termOld);

							// change terminal settings to avoid waiting for a newline before getting data
							termNew=termOld;
							termNew.c_lflag&=~ICANON;
							tcsetattr(STDIN_FILENO, TCSANOW, &termNew);
							#endif

							// attempt to read
							uint8_t value;
							KernelFsFileOffset readResult=kernelFsFileReadOffset(fd, 0, &value, 1, false);

							#ifndef ARDUINO
							// restore terminal settings
							tcsetattr(STDIN_FILENO, TCSANOW, &termOld);
							#endif

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
							procData->regs[0]=procData->stdioFd;
						break;
						case ByteCodeSyscallIdEnvSetStdioFd:
							procData->stdioFd=procData->regs[1];
						break;
						case ByteCodeSyscallIdEnvGetPwd: {
							char pwd[KernelFsPathMax];
							if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->pwd, pwd, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during envgetpwd syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(pwd);

							if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], pwd)) {
								kernelLog(LogTypeWarning, "failed during envgetpwd syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
						} break;
						case ByteCodeSyscallIdEnvSetPwd: {
							ByteCodeWord addr=procData->regs[1];
							if (addr<ByteCodeMemoryRamAddr) {
								kernelLog(LogTypeWarning, "failed during envsetpwd syscall - addr 0x%04X does not point to RW region, process %u (%s), killing\n", addr, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}

							KernelFsFileOffset ramIndex=addr-ByteCodeMemoryRamAddr;
							procData->pwd=ramIndex+procData->envVarDataLen;
						} break;
						case ByteCodeSyscallIdEnvGetPath: {
							char path[ProcManEnvVarPathMax];
							if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->path, path, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during envgetpath syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}

							if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], path)) {
								kernelLog(LogTypeWarning, "failed during envgetpath syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
						} break;
						case ByteCodeSyscallIdEnvSetPath: {
							ByteCodeWord addr=procData->regs[1];
							if (addr<ByteCodeMemoryRamAddr) {
								kernelLog(LogTypeWarning, "failed during envsetpath syscall - addr 0x%04X does not point to RW region, process %u (%s), killing\n", addr, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}

							KernelFsFileOffset ramIndex=addr-ByteCodeMemoryRamAddr;
							procData->path=ramIndex+procData->envVarDataLen;
						} break;
						case ByteCodeSyscallIdTimeMonotonic:
							procData->regs[0]=(millis()/1000);
						break;
						case ByteCodeSyscallIdRegisterSignalHandler: {
							uint16_t signalId=procData->regs[1];
							uint16_t handlerAddr=procData->regs[2];

							if (signalId>=ByteCodeSignalIdNB) {
								kernelLog(LogTypeWarning, "process %u (%s), tried to register handler for invalid signal %u\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), signalId);
							} else if (handlerAddr>=256) {
								kernelLog(LogTypeWarning, "process %u (%s), tried to register handler for signal %u, but handler addr %u is out of range (must be less than 256)\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), signalId, handlerAddr);
							} else
								procData->signalHandlers[signalId]=handlerAddr;

						} break;
						case ByteCodeSyscallIdShutdown:
							// Kill all processes, causing kernel to return/halt
							kernelLog(LogTypeInfo, "process %u (%s) initiated shutdown via syscall\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
							kernelShutdownBegin();
						break;
						case ByteCodeSyscallIdMount: {
							// Grab arguments
							uint16_t format=procData->regs[1];
							uint16_t devicePathAddr=procData->regs[2];
							uint16_t dirPathAddr=procData->regs[3];

							char devicePath[KernelFsPathMax], dirPath[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, devicePathAddr, devicePath, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during mount syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							if (!procManProcessMemoryReadStr(process, procData, dirPathAddr, dirPath, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during mount syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(devicePath);
							kernelFsPathNormalise(dirPath);

							// Attempt to mount
							procData->regs[0]=kernelMount(format, devicePath, dirPath);
						} break;
						case ByteCodeSyscallIdUnmount: {
							// Grab arguments
							uint16_t devicePathAddr=procData->regs[1];

							char devicePath[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, devicePathAddr, devicePath, KernelFsPathMax)) {
								kernelLog(LogTypeWarning, "failed during mount syscall, process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								return false;
							}
							kernelFsPathNormalise(devicePath);

							// Unmount
							kernelUnmount(devicePath);
						} break;
						case ByteCodeSyscallIdIoctl: {
							// Grab arguments
							uint16_t fd=procData->regs[1];
							uint16_t command=procData->regs[2];
							uint16_t data=procData->regs[3];

							// We currently only support ioctl on /dev/ttyS0
							if (strcmp(kernelFsGetFilePath(fd), "/dev/ttyS0")==0) {
								switch(command) {
									case ByteCodeSyscallIdIoctlCommandSetEcho: {
										#ifdef ARDUINO
										kernelDevTtyS0EchoFlag=data;
										#else
										// change terminal settings to add/remove echo
										static struct termios termSettings;
										tcgetattr(STDIN_FILENO, &termSettings);
										if (data)
											termSettings.c_lflag|=ECHO;
										else
											termSettings.c_lflag&=~ECHO;
										tcsetattr(STDIN_FILENO, TCSANOW, &termSettings);
										#endif
									} break;
									default:
										kernelLog(LogTypeWarning, "invalid ioctl syscall command %u (on fd %u, device '%s'), process %u (%s)\n", command, fd, kernelFsGetFilePath(fd), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
									break;
								}
							}
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
	KernelFsFd childRamFd=KernelFsFdInvalid;
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
	uint16_t ramTotalSize=procData->envVarDataLen+procData->ramLen;
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

	childRamFd=kernelFsFileOpen(childRamPath);
	if (childRamFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not open child ram data file at '%s'\n", parentPid, childRamPath);
		goto error;
	}

	// Simply use same FD as parent for the program data
	child->state=ProcManProcessStateActive;
	child->progmemFd=procManData.processes[parentPid].progmemFd;
	child->instructionCounter=0;

	// Initialise proc file
	KernelFsFd savedFd=procData->ramFd;
	ByteCodeWord savedR0=procData->regs[0];
	procData->ramFd=childRamFd;
	procData->regs[0]=0; // indicate success in the child
	bool storeRes=procManProcessStoreProcData(child, procData);
	procData->ramFd=savedFd;
	procData->regs[0]=savedR0;
	if (!storeRes) {
		kernelLog(LogTypeWarning, "could not fork from %u - could not save child process data file to '%s'\n", parentPid, childProcPath);
		goto error;
	}

	// Copy parent's ram into child's
	for(KernelFsFileOffset i=0; i<ramTotalSize; ++i) {
		bool res=true;
		uint8_t value;
		res&=(kernelFsFileReadOffset(procData->ramFd, i, &value, 1, false)==1);
		res&=(kernelFsFileWriteOffset(childRamFd, i, &value, 1)==1);
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
		kernelFsFileClose(childRamFd);
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
	char args[ARGVMAX][ProcManArgLenMax];

	for(uint8_t i=0; i<ARGVMAX; ++i)
		args[i][0]='\0';

	if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], args[0], KernelFsPathMax)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not read path argument\n", procManGetPidFromProcess(process));
		return false;
	}

	for(uint8_t i=1; i<ARGVMAX; ++i)
		if (procData->regs[i+1]!=0)
			if (!procManProcessMemoryReadStr(process, procData, procData->regs[i+1], args[i], KernelFsPathMax)) {
				kernelLog(LogTypeWarning, "exec in %u failed - could not read argument %u\n", procManGetPidFromProcess(process), i);
				return false;
			}

	kernelLog(LogTypeInfo, "exec in %u - raw path '%s', arg1='%s', arg2='%s', arg3='%s'\n", procManGetPidFromProcess(process), args[0], args[1], args[2], args[3]); // TODO: Avoid hardcoded number of arguments

	// Grab pwd and path env vars as these may now point into general ram, which is about to be cleared when we resize
	char tempPwd[KernelFsPathMax];
	if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->pwd, tempPwd, KernelFsPathMax)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not read env var pwd at offset %u\n", procManGetPidFromProcess(process), procData->pwd);
		return false;
	}
	kernelFsPathNormalise(tempPwd);

	char tempPath[ProcManEnvVarPathMax];
	if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->path, tempPath, KernelFsPathMax)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not read env var path at offset %u\n", procManGetPidFromProcess(process), procData->path);
		return false;
	}
	kernelFsPathNormalise(tempPath);

	// Load program (handling magic bytes)
	KernelFsFd newProgmemFd=procManProcessLoadProgmemFile(process, args);
	if (newProgmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not open progmem file ('%s')\n", procManGetPidFromProcess(process), args[0]);
		return true;
	}

	// Close old fd (if not shared)
	ProcManPid pid=procManGetPidFromProcess(process);
	KernelFsFd oldProgmemFd=procManData.processes[pid].progmemFd;
	procManData.processes[pid].progmemFd=newProgmemFd;

	ProcManPid i;
	for(i=0; i<ProcManPidMax; ++i)
		if (procManData.processes[i].progmemFd==oldProgmemFd)
			break;
	if (i==ProcManPidMax)
		kernelFsFileClose(oldProgmemFd);

	// Reset instruction pointer
	procData->regs[ByteCodeRegisterIP]=0;

	// Update env var array and calculate new len
	procData->envVarDataLen=0;

	procData->pwd=procData->envVarDataLen;
	procData->envVarDataLen+=strlen(tempPwd)+1;
	procData->path=procData->envVarDataLen;
	procData->envVarDataLen+=strlen(tempPath)+1;

	for(uint8_t i=0; i<ARGVMAX; ++i) {
		uint16_t argLen=strlen(args[i]);
		procData->argv[i]=procData->envVarDataLen;
		procData->envVarDataLen+=argLen+1; // +1 for null terminator
	}

	// Resize ram file (clear data and add new arguments)
	procData->ramLen=0; // no ram needed initially
	uint16_t newRamTotalSize=procData->envVarDataLen+procData->ramLen;

	char ramPath[KernelFsPathMax];
	sprintf(ramPath, "/tmp/ram%u", pid);

	kernelFsFileClose(procData->ramFd);
	if (!kernelFsFileResize(ramPath, newRamTotalSize)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not resize new processes RAM file at '%s' to %u\n", procManGetPidFromProcess(process), ramPath, newRamTotalSize);
		return false;
	}
	procData->ramFd=kernelFsFileOpen(ramPath);
	assert(procData->ramFd!=KernelFsFdInvalid);

	// Write env vars into ram file
	if (kernelFsFileWriteOffset(procData->ramFd, procData->pwd, (const uint8_t *)(tempPwd), strlen(tempPwd)+1)!=strlen(tempPwd)+1) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not write env var pwd into new processes memory\n", procManGetPidFromProcess(process));
		return false;
	}

	if (kernelFsFileWriteOffset(procData->ramFd, procData->path, (const uint8_t *)(tempPath), strlen(tempPath)+1)!=strlen(tempPath)+1) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not write env var path into new processes memory\n", procManGetPidFromProcess(process));
		return false;
	}

	for(uint8_t i=0; i<ARGVMAX; ++i) {
		uint16_t argSize=strlen(args[i])+1;
		if (kernelFsFileWriteOffset(procData->ramFd, procData->argv[i], (const uint8_t *)(args[i]), argSize)!=argSize) {
			kernelLog(LogTypeWarning, "exec in %u failed - could not write args into new processes memory\n", procManGetPidFromProcess(process));
			return false;
		}
	}

	// Clear any registered signal handlers.
	for(uint16_t i=0; i<ByteCodeSignalIdNB; ++i)
		procData->signalHandlers[i]=ProcManSignalHandlerInvalid;

	// Save proc data
	if (!procManProcessStoreProcData(process, procData)) {
		kernelLog(LogTypeWarning, "exec in %u failed - could not store procdata\n", procManGetPidFromProcess(process));
		return false;
	}

	kernelLog(LogTypeInfo, "exec in %u succeeded\n", procManGetPidFromProcess(process));

	return true;
}

KernelFsFd procManProcessLoadProgmemFile(ProcManProcess *process, char args[ARGVMAX][ProcManArgLenMax]) {
	assert(process!=NULL);

	// Normalise path and then check if valid
	kernelFsPathNormalise(args[0]);

	if (!kernelFsPathIsValid(args[0])) {
		kernelLog(LogTypeWarning, "loading executable in %u failed - path '%s' not valid\n", procManGetPidFromProcess(process), args[0]);
		return KernelFsFdInvalid;
	}

	if (kernelFsFileIsDir(args[0])) {
		kernelLog(LogTypeWarning, "loading executable in %u failed - path '%s' is a directory\n", procManGetPidFromProcess(process), args[0]);
		return KernelFsFdInvalid;
	}

	// Attempt to read magic bytes and handle any special logic such as the '#!' shebang syntax
	KernelFsFd newProgmemFd=KernelFsFdInvalid;
	uint8_t magicByteRecursionCount, magicByteRecursionCountMax=8;
	for(magicByteRecursionCount=0; magicByteRecursionCount<magicByteRecursionCountMax; ++magicByteRecursionCount) {
		// Attempt to open program file
		newProgmemFd=kernelFsFileOpen(args[0]);
		if (newProgmemFd==KernelFsFdInvalid) {
			kernelLog(LogTypeWarning, "loading executable in %u failed - could not open program at '%s'\n", procManGetPidFromProcess(process), args[0]);
			return KernelFsFdInvalid;
		}

		// Read first two bytes to decide how to execute
		uint8_t magicBytes[2];
		if (kernelFsFileRead(newProgmemFd, magicBytes, 2)!=2) {
			kernelLog(LogTypeWarning, "loading executable in %u failed - could not read 2 magic bytes at start of '%s', fd %u\n", procManGetPidFromProcess(process), args[0], newProgmemFd);
			kernelFsFileClose(newProgmemFd);
			return KernelFsFdInvalid;
		}

		if (magicBytes[0]=='G' && magicBytes[1]=='G') {
			// A standard native executable - no special handling required (the magic bytes run as harmless instructions)
			break;
		} else if (magicBytes[0]=='#' && magicBytes[1]=='!') {
			// An interpreter (with path following after '#!') should be used instead to run this file.

			// Read interpreter path string
			char interpreterPath[KernelFsPathMax+2];
			KernelFsFileOffset readCount=kernelFsFileReadOffset(newProgmemFd, 2, (uint8_t *)interpreterPath, KernelFsPathMax+2, false);
			interpreterPath[readCount-1]='\0';

			// Look for newline and if found terminate string here
			char *newlinePtr=strchr(interpreterPath, '\n');
			if (newlinePtr==NULL) {
				kernelLog(LogTypeWarning, "loading executable in %u failed - '#!' not followed by interpreter path (original exec path '%s', fd %u)\n", procManGetPidFromProcess(process), args[0], newProgmemFd);
				kernelFsFileClose(newProgmemFd);
				return KernelFsFdInvalid;
			}
			*newlinePtr='\0';

			// Write to log
			kernelFsPathNormalise(interpreterPath);
			kernelLog(LogTypeInfo, "loading exeutable in %u - magic bytes '#!' detected, using interpreter '%s' (original exec path '%s', fd %u)\n", procManGetPidFromProcess(process), interpreterPath, args[0], newProgmemFd);

			// Close the original progmem file
			kernelFsFileClose(newProgmemFd);

			// Update args
			strcpy(args[1], args[0]);
			strcpy(args[0], interpreterPath);
			for(uint8_t i=2; i<ARGVMAX; ++i)
				strcpy(args[i], "");

			// Loop again in an attempt to load interpreter
		} else {
			kernelLog(LogTypeWarning, "loading executable in %u failed - unknown magic byte sequence 0x%02X%02X at start of '%s', fd %u\n", procManGetPidFromProcess(process), magicBytes[0], magicBytes[1], args[0], newProgmemFd);
			kernelFsFileClose(newProgmemFd);
			return KernelFsFdInvalid;
		}
	}

	if (newProgmemFd==KernelFsFdInvalid || magicByteRecursionCount==magicByteRecursionCountMax) {
		kernelLog(LogTypeWarning, "loading executable in %u failed - could not open progmem file (shebang infinite recursion perhaps?) ('%s', fd %u)\n", procManGetPidFromProcess(process), args[0], newProgmemFd);
		return KernelFsFdInvalid;
	}

	return newProgmemFd;
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
	for(ProcManPid i=0; i<ProcManPidMax; ++i)
		procManData.processes[i].instructionCounter=0;
}
