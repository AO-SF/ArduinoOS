#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef ARDUINO
#include <alloca.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "hwdevice.h"
#include "kernel.h"
#include "kernelmount.h"
#include "ktime.h"
#include "log.h"
#include "pins.h"
#include "procman.h"
#include "spi.h"

#define procManProcessInstructionCounterMax (65500u) // largest 16 bit unsigned number, less a small safety margin
#define procManProcessInstructionsPerTick 160 // generally a higher value causes faster execution, but decreased responsiveness if many processes running
#define procManTicksPerInstructionCounterReset (procManProcessInstructionCounterMax/procManProcessInstructionsPerTick)

#define ProcManSignalHandlerInvalid 0

#define ProcManArgLenMax 64
#define ProcManEnvVarPathMax 128

typedef enum {
	ProcManProcessStateUnused,
	ProcManProcessStateActive,
	ProcManProcessStateWaitingWaitpid,
	ProcManProcessStateWaitingRead,
	ProcManProcessStateExiting,
} ProcManProcessState;

#define ARGVMAX 255 // this is due to using 8 bits for argc

struct ProcManProcessProcData {
	// Process data
	BytecodeWord regs[BytecodeRegisterNB];
	uint8_t signalHandlers[BytecodeSignalIdNB]; // pointers to functions to run on signals (restricted to first 256 bytes)
	uint16_t ramLen;
	uint8_t envVarDataLen;
	uint8_t ramFd;
	KernelFsFd fds[ProcManMaxFds];

	// Environment variables
	KernelFsFd stdinFd; // set to KernelFsFdInvalid when init is called
	KernelFsFd stdoutFd; // set to KernelFsFdInvalid when init is called

	// The following fields are pointers into the start of the ramFd file.
	// The arguments are fixed, but pwd and path may be updated later by the process itself to point to new strings, beyond the initial read-only section (and so need a full 16 bit offset).
	uint8_t argc; // argument count
	uint8_t argvStart; // ptr to start of char array representing 1st arg, followed directly by one for 2nd arg, etc
	uint16_t pwd; // set to '/' when init is called
	uint16_t path; // set to '/usr/games:/usr/bin:/bin:' when init is called
};

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

char procManScratchBufPath0[KernelFsPathMax];
char procManScratchBufPath1[KernelFsPathMax];
char procManScratchBufPath2[KernelFsPathMax];
char procManScratchBuf256[256];

#define ProcManPrefetchDataBufferSize 32
typedef struct {
	uint8_t buffer[ProcManPrefetchDataBufferSize];
	uint16_t baseAddr, len;
} ProcManPrefetchData;

void procManPrefetchDataClear(ProcManPrefetchData *pd);
bool procManPrefetchDataReadByte(ProcManPrefetchData *pd, ProcManProcess *process, ProcManProcessProcData *procData, uint16_t addr, uint8_t *value);

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid);
ProcManPid procManGetPidFromProcess(const ProcManProcess *process);
const char *procManGetExecPathFromProcess(const ProcManProcess *process);

ProcManPid procManFindUnusedPid(void);

bool procManProcessLoadProcData(const ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessStoreProcData(ProcManProcess *process, ProcManProcessProcData *procData);

bool procManProcessLoadProcDataRamLen(const ProcManProcess *process, uint16_t *value);
bool procManProcessLoadProcDataEnvVarDataLen(const ProcManProcess *process, uint8_t *value);
bool procManProcessLoadProcDataRamFd(const ProcManProcess *process, KernelFsFd *ramFd);
bool procManProcessSaveProcDataReg(const ProcManProcess *process, BytecodeRegister reg, BytecodeWord value);

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t *value);
bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord *value);
bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, char *str, uint16_t len);
bool procManProcessMemoryReadBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t *data, uint16_t len, bool verbose); // block should not cross split in memory between two types
bool procManProcessMemoryReadByteAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, uint8_t *value);
bool procManProcessMemoryReadWordAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, BytecodeWord *value);
bool procManProcessMemoryReadStrAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, char *str, uint16_t len);
bool procManProcessMemoryReadBlockAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, uint8_t *data, uint16_t len, bool verbose);
bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t value);
bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord value);
bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const char *str);
bool procManProcessMemoryWriteBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const uint8_t *data, uint16_t len); // Note: addr with len should not cross over the boundary between the two parts of memory.

bool procManProcessGetArgvN(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char str[ProcManArgLenMax]); // Returns false to indicate illegal memory operation. Always succeeds otherwise, but str may be 0 length.

bool procManProcessGetInstruction(ProcManProcess *process, ProcManProcessProcData *procData, ProcManPrefetchData *prefetchData, BytecodeInstruction3Byte *instruction);
bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstruction3Byte instruction, ProcManPrefetchData *prefetchData, ProcManExitStatus *exitStatus);
bool procManProcessExecInstructionMemory(ProcManProcess *process, ProcManProcessProcData *procData, const BytecodeInstructionInfo *info, ProcManExitStatus *exitStatus);
bool procManProcessExecInstructionAlu(ProcManProcess *process, ProcManProcessProcData *procData, const BytecodeInstructionInfo *info, ProcManPrefetchData *prefetchData, ProcManExitStatus *exitStatus);
bool procManProcessExecInstructionMisc(ProcManProcess *process, ProcManProcessProcData *procData, const BytecodeInstructionInfo *info, ProcManPrefetchData *prefetchData, ProcManExitStatus *exitStatus);
bool procManProcessExecSyscall(ProcManProcess *process, ProcManProcessProcData *procData, ProcManExitStatus *exitStatus);

void procManProcessFork(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData); // Returns false only on critical error (e.g. segfault), i.e. may return true even though exec operation itself failed
bool procManProcessExec2(ProcManProcess *process, ProcManProcessProcData *procData); // See procManProcessExec
bool procManProcessExecCommon(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t argc, char *argv);

KernelFsFd procManProcessLoadProgmemFile(ProcManProcess *process, uint8_t *argc, char *argvStart, const char *envPath, const char *envPwd); // Loads executable tiles, reading the magic byte (and potentially recursing), before returning fd of final executable (or KernelFsFdInvalid on failure)

bool procManProcessRead(ProcManProcess *process, ProcManProcessProcData *procData);

void procManResetInstructionCounters(void);

char *procManArgvStringGetArgN(uint8_t argc, char *argvStart, uint8_t n);
const char *procManArgvStringGetArgNConst(uint8_t argc, const char *argvStart, uint8_t n);
int procManArgvStringGetTotalSize(uint8_t argc, const char *argvStart); // total size used for entire combined string, including all null bytes
void procManArgvStringPathNormaliseArg0(uint8_t argc, char *argvStart);
void procManArgvUpdateForInterpreter(uint8_t *argc, char *argvStart, const char *interpreterPath);
void procManArgvDebug(uint8_t argc, const char *argvStart);

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
#define procPath procManScratchBufPath0
#define ramPath procManScratchBufPath1
#define tempPath procManScratchBufPath2
#define envVarData procManScratchBuf256 // TODO: Should be larger for worst-case
	KernelFsFd ramFd=KernelFsFdInvalid;
	ProcManProcessProcData procData;

	kernelLog(LogTypeInfo, kstrP("attempting to create new process at '%s'\n"), programPath);

	// Find a PID for the new process
	ProcManPid pid=procManFindUnusedPid();
	if (pid==ProcManPidMax) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - no spare PIDs\n"));
		return ProcManPidMax;
	}

	// Construct tmp paths
	sprintf(procPath, "/tmp/proc%u", pid);
	sprintf(ramPath, "/tmp/ram%u", pid);

	// Load program (handling magic bytes)
	uint8_t argc=1;
	char argvStart[2*KernelFsPathMax]; // spare space is due to potential interpreter path added by procManProcessLoadProgmemFile
	strcpy(argvStart, programPath);

	procManData.processes[pid].progmemFd=procManProcessLoadProgmemFile(&procManData.processes[pid], &argc, argvStart, NULL, NULL);
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not open progmem file ('%s')\n"), argvStart);
		goto error;
	}

	// Create env vars data
	uint8_t envVarDataLen=0;

	char tempPwd[KernelFsPathMax];
	strcpy(tempPwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(tempPwd, &dirname, &basename);
	assert(dirname==tempPwd);
	procData.pwd=envVarDataLen;
	strcpy(envVarData+envVarDataLen, dirname);
	envVarDataLen+=strlen(dirname)+1;

	strcpy(tempPath, "/usr/games:/usr/bin:/bin:");
	procData.path=envVarDataLen;
	strcpy(envVarData+envVarDataLen, tempPath);
	envVarDataLen+=strlen(tempPath)+1;

	procData.argc=argc;
	procData.argvStart=envVarDataLen;
	int argvTotalSize=procManArgvStringGetTotalSize(argc, argvStart);
	memcpy(envVarData+envVarDataLen, argvStart, argvTotalSize);
	envVarDataLen+=argvTotalSize;

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(procPath, sizeof(ProcManProcessProcData))) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not create process data file at '%s' of size %u\n"), procPath, sizeof(ProcManProcessProcData));
		goto error;
	}
	if (!kernelFsFileCreateWithSize(ramPath, envVarDataLen)) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not create ram data file at '%s' of size %u\n"), ramPath, envVarDataLen);
		goto error;
	}

	// Attempt to open proc and ram files
	procManData.processes[pid].procFd=kernelFsFileOpen(procPath);
	if (procManData.processes[pid].procFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not open process data file at '%s'\n"), procPath);
		goto error;
	}

	ramFd=kernelFsFileOpen(ramPath);
	if (ramFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not open ram data file at '%s'\n"), ramPath);
		goto error;
	}

	// Initialise state
	procManData.processes[pid].state=ProcManProcessStateActive;
	procManData.processes[pid].instructionCounter=0;

	// Initialise proc file (and env var data in ram file)
	procData.regs[BytecodeRegisterIP]=0;
	for(BytecodeSignalId i=0; i<BytecodeSignalIdNB; ++i)
		procData.signalHandlers[i]=ProcManSignalHandlerInvalid;
	procData.stdinFd=KernelFsFdInvalid;
	for(unsigned i=0; i<ProcManMaxFds; ++i)
		procData.fds[i]=KernelFsFdInvalid;
	procData.stdoutFd=KernelFsFdInvalid;
	procData.envVarDataLen=envVarDataLen;
	procData.ramLen=0;
	procData.ramFd=ramFd;

	if (kernelFsFileWriteOffset(ramFd, 0, (uint8_t *)envVarData, envVarDataLen)!=envVarDataLen) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not write env var data into ram file '%s', fd %u (tried %u bytes)\n"), ramPath, ramFd, envVarDataLen);
		goto error;
	}

	if (!procManProcessStoreProcData(&procManData.processes[pid], &procData)) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not save process data file\n"));
		goto error;
	}

	kernelLog(LogTypeInfo, kstrP("created new process '%s' with PID %u\n"), programPath, pid);

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
#undef procPath
#undef ramPath
#undef tempPath
#undef envVarData
}

void procManKillAll(void) {
	for(ProcManPid i=0; i<ProcManPidMax; ++i)
		if (procManGetProcessByPid(i)!=NULL)
			procManProcessKill(i, ProcManExitStatusKilled, NULL);
}

void procManProcessKill(ProcManPid pid, ProcManExitStatus exitStatus, const ProcManProcessProcData *procDataGiven) {
	kernelLog(LogTypeInfo, kstrP("attempting to kill process %u with exit status %u\n"), pid, exitStatus);

	// Not even open?
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL) {
		kernelLog(LogTypeWarning, kstrP("could not kill process %u - no such process\n"), pid);
		return;
	}

	// Attempt to get/load proc data, but note this is not critical (after all, we may be here precisely because we could not read the proc data file).
	const ProcManProcessProcData *procData=NULL;
	ProcManProcessProcData procDataRaw;
	if (procDataGiven!=NULL)
		procData=procDataGiven;
	else if (procManProcessLoadProcData(process, &procDataRaw))
		procData=&procDataRaw;

	// Close files left open by process
	if (procData!=NULL)
		for(unsigned i=0; i<ProcManMaxFds; ++i)
			// Have we found a file left unclosed?
			if (procData->fds[i]!=KernelFsFdInvalid) {
				// Check file is actually open according to the VFS
				KStr pathKStr=kernelFsGetFilePath(procData->fds[i]);
				if (kstrIsNull(pathKStr)) {
					kernelLog(LogTypeWarning, kstrP("killing process %u - tried to close fd=%u from fds table (index %u), but not open\n"), pid, procData->fds[i], i);
				} else {
					// Write to log
					kstrStrcpy(procManScratchBuf256, pathKStr);
					kernelLog(LogTypeInfo, kstrP("killing process %u - closing open file '%s', fd=%u, fdsindex=%u\n"), pid, procManScratchBuf256, procData->fds[i], i);

					// Close file
					kernelFsFileClose(procData->fds[i]);
				}
			}

	// Close proc and ram files, deleting tmp ones
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
		KernelFsFd ramFd;
		if (procManProcessLoadProcDataRamFd(process, &ramFd) && ramFd!=KernelFsFdInvalid) {
			char ramPath[KernelFsPathMax];
			kstrStrcpy(ramPath, kernelFsGetFilePath(ramFd));
			kernelFsFileClose(ramFd);
			kernelFsFileDelete(ramPath);
		}

		// Close and delete proc file
		char procPath[KernelFsPathMax];
		kstrStrcpy(procPath, kernelFsGetFilePath(process->procFd));

		kernelFsFileClose(process->procFd);
		process->procFd=KernelFsFdInvalid;

		kernelFsFileDelete(procPath);
	}

	// Reset state
	process->state=ProcManProcessStateUnused;
	process->instructionCounter=0;

	// Write to log
	if (procData!=NULL)
		kernelLog(LogTypeInfo, kstrP("killed process %u (post-IP=%u)\n"), pid, procData->regs[BytecodeRegisterIP]);
	else
		kernelLog(LogTypeInfo, kstrP("killed process %u\n"), pid);

	// Check if any processes are waiting due to waitpid syscall
	for(ProcManPid waiterPid=0; waiterPid<ProcManPidMax; ++waiterPid) {
		ProcManProcess *waiterProcess=procManGetProcessByPid(waiterPid);
		if (waiterProcess!=NULL && waiterProcess->state==ProcManProcessStateWaitingWaitpid && waiterProcess->waitingData8==pid) {
			// Bring this process back to life, storing the exit status into r0
			BytecodeWord r0Value=exitStatus;
			if (!procManProcessSaveProcDataReg(waiterProcess, 0, r0Value)) {
				kernelLog(LogTypeWarning, kstrP("process %u died - could not wake process %u from waitpid syscall (could not save r0 proc data)\n"), pid, waiterPid);
			} else {
				kernelLog(LogTypeInfo, kstrP("process %u died - woke process %u from waitpid syscall\n"), pid, waiterPid);
				waiterProcess->state=ProcManProcessStateActive;
			}
		}
	}
}

void procManProcessTick(ProcManPid pid) {
	ProcManExitStatus exitStatus=ProcManExitStatusKilled;
	bool procDataLoaded=false;

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
				kernelLog(LogTypeWarning, kstrP("process %u tick (active) - could not load proc data, killing\n"), pid);
				goto kill;
			}
		} break;
		case ProcManProcessStateWaitingWaitpid: {
			// Is this process waiting for a timeout, and that time has been reached?
			if (process->waitingData16>0 && ktimeGetMs()/1000>=process->waitingData16) {
				// It has - load process data so we can update the state and set r0 to indicate a timeout occured
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (waitpid timeout) - could not load proc data, killing\n"), pid);
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
					kernelLog(LogTypeWarning, kstrP("process %u tick (read available) - could not load proc data, killing\n"), pid);
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
			kernelLog(LogTypeWarning, kstrP("process %u tick - internal error: state is exiting at tick init, killing\n"), pid);
			goto kill;
		break;
	}

	procDataLoaded=true;

	// Run a few instructions
	ProcManPrefetchData prefetchData;
	procManPrefetchDataClear(&prefetchData);
	for(uint16_t instructionNum=0; instructionNum<procManProcessInstructionsPerTick; ++instructionNum) {
		// Run a single instruction
		BytecodeInstruction3Byte instruction;
		if (!procManProcessGetInstruction(process, &procData, &prefetchData, &instruction)) {
			kernelLog(LogTypeWarning, kstrP("process %u tick - could not get instruction, killing\n"), pid);
			goto kill;
		}

		// Execute instruction
		if (!procManProcessExecInstruction(process, &procData, instruction, &prefetchData, &exitStatus)) {
			if (process->state!=ProcManProcessStateExiting)
				kernelLog(LogTypeWarning, kstrP("process %u tick - could not exec instruction or returned false, killing\n"), pid);
			goto kill;
		}

		// Increment instruction counter
		assert(process->instructionCounter<procManProcessInstructionCounterMax); // we reset often enough to prevent this
		++process->instructionCounter;

		// Has this process gone inactive?
		if (procManData.processes[pid].state!=ProcManProcessStateActive)
			break;
	}

	// Save tmp data
	if (!procManProcessStoreProcData(process, &procData)) {
		kernelLog(LogTypeWarning, kstrP("process %u tick - could not store proc data post tick, killing\n"), pid);
		goto kill;
	}

	return;

	kill:
	procManProcessKill(pid, exitStatus, (procDataLoaded ? &procData : NULL));
}

void procManProcessSendSignal(ProcManPid pid, BytecodeSignalId signalId) {
	// Check signal id
	if (signalId>BytecodeSignalIdNB) {
		kernelLog(LogTypeWarning, kstrP("could not send signal %u to process %u, bad signal id\n"), signalId, pid);
		return;
	}

	// Find process
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL) {
		kernelLog(LogTypeWarning, kstrP("could not send signal %u to process %u, no such process\n"), signalId, pid);
		return;
	}

	// Load process' data.
	ProcManProcessProcData procData;
	if (!procManProcessLoadProcData(process, &procData)) {
		kernelLog(LogTypeWarning, kstrP("could not send signal %u to process %u, could not load process ddata\n"), signalId, pid);
		return;
	}

	// Look for a registered handler.
	uint16_t handlerAddr=procData.signalHandlers[signalId];
	if (handlerAddr==0) {
		kernelLog(LogTypeInfo, kstrP("sent signal %u to process %u, but no handler registered so ignoring\n"), signalId, pid);
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
			kernelLog(LogTypeWarning, kstrP("sent signal %u to process %u, but its state is exiting (internal error), ignoring\n"), signalId, pid);
			return;
		break;
	}
	assert(process->state==ProcManProcessStateActive);

	// 'Call' the registered handler (in the same way the assembler generates call instructions)
	// Do this by pushing the current IP as the return address, before jumping into handler.
	procManProcessMemoryWriteWord(process, &procData, procData.regs[BytecodeRegisterSP], procData.regs[BytecodeRegisterIP]); // TODO: Check return
	procData.regs[BytecodeRegisterSP]+=2;
	procData.regs[BytecodeRegisterIP]=handlerAddr;

	// Save process' data.
	if (!procManProcessStoreProcData(process, &procData)) {
		kernelLog(LogTypeInfo, kstrP("could not send signal %u to process %u, could not store process ddata\n"), signalId, pid);
		return;
	}

	kernelLog(LogTypeInfo, kstrP("sent signal %u to process %u, calling registered handler at %u\n"), signalId, pid, handlerAddr);
}

bool procManProcessExists(ProcManPid pid) {
	return (procManGetProcessByPid(pid)!=NULL);
}

bool procManProcessGetOpenFds(ProcManPid pid, KernelFsFd fds[ProcManMaxFds]) {
	// Grab process (if exists)
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return false;

	// Load proc data
	ProcManProcessProcData procData;
	if (!procManProcessLoadProcData(process, &procData))
		return false;

	// Copy fds table
	memcpy(fds, procData.fds, sizeof(procData.fds));

	return true;
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
	if (kstrIsNull(kernelFsGetFilePath(process->progmemFd)))
		return NULL;
	kstrStrcpy(procManScratchBufPath2, kernelFsGetFilePath(process->progmemFd));
	return procManScratchBufPath2;
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

bool procManProcessLoadProcDataRamLen(const ProcManProcess *process, uint16_t *value) {
	return (process->procFd!=KernelFsFdInvalid && kernelFsFileReadOffset(process->procFd, offsetof(ProcManProcessProcData,ramLen), (uint8_t *)value, sizeof(uint16_t), false)==sizeof(uint16_t));
}

bool procManProcessLoadProcDataEnvVarDataLen(const ProcManProcess *process, uint8_t *value) {
	return (process->procFd!=KernelFsFdInvalid && kernelFsFileReadOffset(process->procFd, offsetof(ProcManProcessProcData,envVarDataLen), value, sizeof(uint8_t), false)==sizeof(uint8_t));
}

bool procManProcessLoadProcDataRamFd(const ProcManProcess *process, KernelFsFd *ramFd) {
	return (process->procFd!=KernelFsFdInvalid && kernelFsFileReadOffset(process->procFd, offsetof(ProcManProcessProcData,ramFd), (uint8_t *)ramFd, sizeof(KernelFsFd), false)==sizeof(KernelFsFd));
}

bool procManProcessSaveProcDataReg(const ProcManProcess *process, BytecodeRegister reg, BytecodeWord value) {
	return (process->procFd!=KernelFsFdInvalid && kernelFsFileWriteOffset(process->procFd, offsetof(ProcManProcessProcData,regs)+sizeof(BytecodeWord)*reg, (uint8_t *)&value, sizeof(BytecodeWord))==sizeof(BytecodeWord));
}

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t *value) {
	return procManProcessMemoryReadBlock(process, procData, addr, value, 1, true);
}

bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord *value) {
	uint8_t upper, lower;
	if (!procManProcessMemoryReadByte(process, procData, addr, &upper))
		return false;
	if (!procManProcessMemoryReadByte(process, procData, addr+1, &lower))
		return false;
	*value=(((BytecodeWord)upper)<<8)|((BytecodeWord)lower);
	return true;
}

bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, char *str, uint16_t len) {
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

bool procManProcessMemoryReadBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t *data, uint16_t len, bool verbose) {
	if (addr+len<BytecodeMemoryRamAddr) {
		// Addresss is in progmem data
		if (kernelFsFileReadOffset(process->progmemFd, addr, data, len, false)==len)
			return true;
		else {
			if (verbose)
				kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read invalid address (0x%04X, pointing to PROGMEM at offset %u, len %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, addr, len);
			return false;
		}
	} else if (addr>=BytecodeMemoryRamAddr) {
		// Address is in RAM
		BytecodeWord ramIndex=(addr-BytecodeMemoryRamAddr);
		KernelFsFileOffset ramOffset=ramIndex+procData->envVarDataLen;
		return procManProcessMemoryReadBlockAtRamfileOffset(process, procData, ramOffset, data, len, verbose);
	} else
		// Block spans both regions of memory, invalid call.
		return false;
}

bool procManProcessMemoryReadByteAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, uint8_t *value) {
	return procManProcessMemoryReadBlockAtRamfileOffset(process, procData, offset, value, 1, true);
}

bool procManProcessMemoryReadWordAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, BytecodeWord *value) {
	uint8_t upper, lower;
	if (!procManProcessMemoryReadByteAtRamfileOffset(process, procData, offset, &upper))
		return false;
	if (!procManProcessMemoryReadByteAtRamfileOffset(process, procData, offset+1, &lower))
		return false;
	*value=(((BytecodeWord)upper)<<8)|((BytecodeWord)lower);
	return true;
}

bool procManProcessMemoryReadStrAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, char *str, uint16_t len) {
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

bool procManProcessMemoryReadBlockAtRamfileOffset(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord offset, uint8_t *data, uint16_t len, bool verbose) {
	KernelFsFileOffset ramTotalSize=procData->envVarDataLen+procData->ramLen;
	if (offset+len<ramTotalSize) {
		if (kernelFsFileReadOffset(procData->ramFd, offset, data, len, false)!=len) {
			if (verbose)
				kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read valid address (RAM file offset %u, len %u) but failed, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), offset, len);
			return false;
		}
		return true;
	} else {
		if (verbose)
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read invalid address (RAM file offset %u, len %u, but size is only %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), offset, len, ramTotalSize);
		return false;
	}
}

bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t value) {
	return procManProcessMemoryWriteBlock(process, procData, addr, &value, 1);
}

bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord value) {
	if (!procManProcessMemoryWriteByte(process, procData, addr, (value>>8)))
		return false;
	if (!procManProcessMemoryWriteByte(process, procData, addr+1, (value&0xFF)))
		return false;
	return true;
}

bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const char *str) {
	return procManProcessMemoryWriteBlock(process, procData, addr, (const uint8_t *)str, strlen(str)+1);
}

bool procManProcessMemoryWriteBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const uint8_t *data, uint16_t len) {
	// Is addr split across boundary?
	if (addr<BytecodeMemoryRamAddr && addr+len>=BytecodeMemoryRamAddr)
		return false;

	// Is this addr in read-only progmem section?
	if (addr+len<BytecodeMemoryRamAddr) {
		kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to read-only address (0x%04X, len %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len);
		return false;
	}

	// addr is in RAM
	BytecodeWord ramIndex=(addr-BytecodeMemoryRamAddr);
	if (ramIndex+len<procData->ramLen) {
		if (kernelFsFileWriteOffset(procData->ramFd, procData->envVarDataLen+ramIndex, data, len)!=len) {
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to valid RAM address (0x%04X, ram offset %u, len %u), but failed, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, len);
			return false;
		}
		return true;
	} else {
		// Close ram file
		char *ramFdPath=alloca(kstrStrlen(kernelFsGetFilePath(procData->ramFd))+1);
		kstrStrcpy(ramFdPath, kernelFsGetFilePath(procData->ramFd));
		kernelFsFileClose(procData->ramFd);
		procData->ramFd=KernelFsFdInvalid;

		// Resize ram file (trying for up to 16 bytes extra, but falling back on minimum if fails)
		KernelFsFileOffset oldRamLen=procData->ramLen;
		uint16_t newRamLenMin=ramIndex+len;
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
				kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to RAM (0x%04X, offset %u, len %u), beyond size, but could not allocate new size (%u vs %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, len, newRamLen, oldRamLen);
				kernelFsFileDelete(ramFdPath);
				goto error;
			}
		}

		// Re-open ram file
		procData->ramFd=kernelFsFileOpen(ramFdPath);
		if (procData->ramFd==KernelFsFdInvalid) {
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to RAM (0x%04X, offset %u, len %u), beyond size, but could not reopen file after resizing, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, len);
			kernelFsFileDelete(ramFdPath);
			goto error;
		}

		// Update stored ram len and write byte
		procData->ramLen=newRamLen;
		if (kernelFsFileWriteOffset(procData->ramFd, procData->envVarDataLen+ramIndex, data, len)!=len) {
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to RAM (0x%04X, offset %u, len %u) had to resize (%u vs %u), but could not write, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, len, newRamLen, oldRamLen);
			goto error;
		}

		return true;

		error:
		// Store procdata back as otherwise when we come to kill ramFd will not be saved
		procManProcessStoreProcData(process, procData); // TODO: Check return
		return false;
	}
}

bool procManProcessGetArgvN(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char str[ProcManArgLenMax]) {
	char *dest=str;

	// Check n is sensible
	if (n>=ARGVMAX || n>=procData->argc) {
		*dest='\0';
		return true;
	}

	// Grab argument
	// Note that we have to loop over all args to find the one in question
	uint16_t index=procData->argvStart;
	while(1) {
		// Read a character
		uint8_t c;
		if (kernelFsFileReadOffset(procData->ramFd, index, &c, 1, false)!=1) {
			kernelLog(LogTypeWarning, kstrP("corrupt argvdata or ram file more generally? Process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			return false;
		}

		// Is this character part of the arg that was asked for?
		if (n==0) {
			*dest++=c;
			if (c=='\0')
				break;
		}

		// Advance to next character, potentially updating n if we have read a whole argument
		++index;
		n-=(c=='\0');
	}

	return true;
}

bool procManProcessGetInstruction(ProcManProcess *process, ProcManProcessProcData *procData, ProcManPrefetchData *prefetchData, BytecodeInstruction3Byte *instruction) {
	if (!procManPrefetchDataReadByte(prefetchData, process, procData, procData->regs[BytecodeRegisterIP]++, ((uint8_t *)instruction)+0))
		return false;
	BytecodeInstructionLength length=bytecodeInstructionParseLength(*instruction);
	if (length==BytecodeInstructionLength2Byte || length==BytecodeInstructionLength3Byte)
		if (!procManPrefetchDataReadByte(prefetchData, process, procData, procData->regs[BytecodeRegisterIP]++, ((uint8_t *)instruction)+1))
			return false;
	if (length==BytecodeInstructionLength3Byte)
		if (!procManPrefetchDataReadByte(prefetchData, process, procData, procData->regs[BytecodeRegisterIP]++, ((uint8_t *)instruction)+2))
			return false;
	return true;
}

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstruction3Byte instruction, ProcManPrefetchData *prefetchData, ProcManExitStatus *exitStatus) {
	// Parse instruction
	BytecodeInstructionInfo info;
	bytecodeInstructionParse(&info, instruction);

	// Execute instruction
	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			return procManProcessExecInstructionMemory(process, procData, &info, exitStatus);
		break;
		case BytecodeInstructionTypeAlu:
			return procManProcessExecInstructionAlu(process, procData, &info, prefetchData, exitStatus);
		break;
		case BytecodeInstructionTypeMisc:
			return procManProcessExecInstructionMisc(process, procData, &info, prefetchData, exitStatus);
		break;
	}

	kernelLog(LogTypeWarning, kstrP("invalid instruction type %i, process %u (%s), killing\n"), info.type, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	return false;
}

bool procManProcessExecInstructionMemory(ProcManProcess *process, ProcManProcessProcData *procData, const BytecodeInstructionInfo *info, ProcManExitStatus *exitStatus) {
	switch(info->d.memory.type) {
		case BytecodeInstructionMemoryTypeStore8:
			if (!procManProcessMemoryWriteByte(process, procData, procData->regs[info->d.memory.destReg], procData->regs[info->d.memory.srcReg])) {
				kernelLog(LogTypeWarning, kstrP("failed during store8 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			return true;
		break;
		case BytecodeInstructionMemoryTypeLoad8: {
			uint8_t value;
			if (!procManProcessMemoryReadByte(process, procData, procData->regs[info->d.memory.srcReg], &value)) {
				kernelLog(LogTypeWarning, kstrP("failed during load8 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			procData->regs[info->d.memory.destReg]=value;
			return true;
		} break;
		case BytecodeInstructionMemoryTypeSet4: {
			procData->regs[info->d.memory.destReg]=info->d.memory.set4Value;
			return true;
		} break;
	}

	kernelLog(LogTypeWarning, kstrP("invalid memory instruction type %i, process %u (%s), killing\n"), info->d.memory.type, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	return false;
}

bool procManProcessExecInstructionAlu(ProcManProcess *process, ProcManProcessProcData *procData, const BytecodeInstructionInfo *info, ProcManPrefetchData *prefetchData, ProcManExitStatus *exitStatus) {
	BytecodeWord opA=procData->regs[info->d.alu.opAReg];
	BytecodeWord opB=procData->regs[info->d.alu.opBReg];
	switch(info->d.alu.type) {
		case BytecodeInstructionAluTypeInc:
			procData->regs[info->d.alu.destReg]+=info->d.alu.incDecValue;
			return true;
		break;
		case BytecodeInstructionAluTypeDec:
			procData->regs[info->d.alu.destReg]-=info->d.alu.incDecValue;
			return true;
		break;
		case BytecodeInstructionAluTypeAdd:
			procData->regs[info->d.alu.destReg]=opA+opB;
			return true;
		break;
		case BytecodeInstructionAluTypeSub:
			procData->regs[info->d.alu.destReg]=opA-opB;
			return true;
		break;
		case BytecodeInstructionAluTypeMul:
			procData->regs[info->d.alu.destReg]=opA*opB;
			return true;
		break;
		case BytecodeInstructionAluTypeDiv:
			if (opB==0) {
				kernelLog(LogTypeWarning, kstrP("division by zero, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			procData->regs[info->d.alu.destReg]=opA/opB;
			return true;
		break;
		case BytecodeInstructionAluTypeXor:
			procData->regs[info->d.alu.destReg]=opA^opB;
			return true;
		break;
		case BytecodeInstructionAluTypeOr:
			procData->regs[info->d.alu.destReg]=opA|opB;
			return true;
		break;
		case BytecodeInstructionAluTypeAnd:
			procData->regs[info->d.alu.destReg]=opA&opB;
			return true;
		break;
		case BytecodeInstructionAluTypeCmp: {
			BytecodeWord *d=&procData->regs[info->d.alu.destReg];
			*d=0;
			*d|=(opA==opB)<<BytecodeInstructionAluCmpBitEqual;
			*d|=(opA==0)<<BytecodeInstructionAluCmpBitEqualZero;
			*d|=(opA!=opB)<<BytecodeInstructionAluCmpBitNotEqual;
			*d|=(opA!=0)<<BytecodeInstructionAluCmpBitNotEqualZero;
			*d|=(opA<opB)<<BytecodeInstructionAluCmpBitLessThan;
			*d|=(opA<=opB)<<BytecodeInstructionAluCmpBitLessEqual;
			*d|=(opA>opB)<<BytecodeInstructionAluCmpBitGreaterThan;
			*d|=(opA>=opB)<<BytecodeInstructionAluCmpBitGreaterEqual;
			return true;
		} break;
		case BytecodeInstructionAluTypeShiftLeft:
			procData->regs[info->d.alu.destReg]=opA<<opB;
			return true;
		break;
		case BytecodeInstructionAluTypeShiftRight:
			procData->regs[info->d.alu.destReg]=opA>>opB;
			return true;
		break;
		case BytecodeInstructionAluTypeSkip: {
			if (procData->regs[info->d.alu.destReg] & (1u<<info->d.alu.opAReg)) {
				// Skip next n instructions
				uint8_t skipDist=info->d.alu.opBReg+1;
				for(uint8_t i=0; i<skipDist; ++i) {
					BytecodeInstruction3Byte skippedInstruction;
					if (!procManProcessGetInstruction(process, procData, prefetchData, &skippedInstruction)) {
						kernelLog(LogTypeWarning, kstrP("could not skip instruction (initial skip dist %u), process %u (%s), killing\n"), skipDist, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
				}
			}
			return true;
		} break;
		case BytecodeInstructionAluTypeExtra: {
			switch((BytecodeInstructionAluExtraType)info->d.alu.opBReg) {
				case BytecodeInstructionAluExtraTypeNot:
					procData->regs[info->d.alu.destReg]=~opA;
					return true;
				break;
				case BytecodeInstructionAluExtraTypeStore16: {
					BytecodeWord destAddr=procData->regs[info->d.alu.destReg];
					if (!procManProcessMemoryWriteWord(process, procData, destAddr, opA)) {
						kernelLog(LogTypeWarning, kstrP("failed during store16 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					return true;
				} break;
				case BytecodeInstructionAluExtraTypeLoad16: {
					BytecodeWord srcAddr=procData->regs[info->d.alu.opAReg];
					if (!procManProcessMemoryReadWord(process, procData, srcAddr, &procData->regs[info->d.alu.destReg])) {
						kernelLog(LogTypeWarning, kstrP("failed during load16 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					return true;
				} break;
				case BytecodeInstructionAluExtraTypePush16: {
					BytecodeWord destAddr=procData->regs[info->d.alu.destReg];
					if (!procManProcessMemoryWriteWord(process, procData, destAddr, opA)) {
						kernelLog(LogTypeWarning, kstrP("failed during push16 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					procData->regs[info->d.alu.destReg]+=2;
					return true;
				} break;
				case BytecodeInstructionAluExtraTypePop16: {
					procData->regs[info->d.alu.opAReg]-=2;
					BytecodeWord srcAddr=procData->regs[info->d.alu.opAReg];
					if (!procManProcessMemoryReadWord(process, procData, srcAddr, &procData->regs[info->d.alu.destReg])) {
						kernelLog(LogTypeWarning, kstrP("failed during load16 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					return true;
				} break;
				case BytecodeInstructionAluExtraTypeCall: {
					// Push return address
					if (!procManProcessMemoryWriteWord(process, procData, opA, procData->regs[BytecodeRegisterIP])) {
						kernelLog(LogTypeWarning, kstrP("failed during call instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					procData->regs[info->d.alu.opAReg]+=2;

					// Jump to call address
					procData->regs[BytecodeRegisterIP]=procData->regs[info->d.alu.destReg];

					return true;
				} break;
				case BytecodeInstructionAluExtraTypeXchg8: {
					uint8_t memValue;
					if (!procManProcessMemoryReadByte(process, procData, procData->regs[info->d.alu.destReg], &memValue)) {
						kernelLog(LogTypeWarning, kstrP("failed during xchg8 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					uint8_t regValue=(procData->regs[info->d.alu.opAReg] & 0xFF);
					if (!procManProcessMemoryWriteByte(process, procData, procData->regs[info->d.alu.destReg], regValue)) {
						kernelLog(LogTypeWarning, kstrP("failed during xchg8 instruction execution, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					procData->regs[info->d.alu.opAReg]=memValue;
					return true;
				} break;
			}

			kernelLog(LogTypeWarning, kstrP("unknown alu extra instruction, type %u, process %u (%s), killing\n"), info->d.alu.opBReg, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			return false;
		} break;
	}

	kernelLog(LogTypeWarning, kstrP("unknown alu instruction type %i, process %u (%s), killing\n"), info->d.alu.type, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	return false;
}

bool procManProcessExecInstructionMisc(ProcManProcess *process, ProcManProcessProcData *procData, const BytecodeInstructionInfo *info, ProcManPrefetchData *prefetchData, ProcManExitStatus *exitStatus) {
	switch(info->d.misc.type) {
		case BytecodeInstructionMiscTypeNop:
		break;
		case BytecodeInstructionMiscTypeSyscall:
			if (!procManProcessExecSyscall(process, procData, exitStatus))
				return false;
			return true;
		break;
		case BytecodeInstructionMiscTypeClearInstructionCache:
			procManPrefetchDataClear(prefetchData);
			return true;
		break;
		case BytecodeInstructionMiscTypeSet8:
			procData->regs[info->d.misc.d.set8.destReg]=info->d.misc.d.set8.value;
			return true;
		break;
		case BytecodeInstructionMiscTypeSet16:
			procData->regs[info->d.misc.d.set16.destReg]=info->d.misc.d.set16.value;
			return true;
		break;
	}

	kernelLog(LogTypeWarning, kstrP("unknown misc instructon type %i, process %u (%s), killing\n"), info->d.misc.type, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	return false;
}

bool procManProcessExecSyscall(ProcManProcess *process, ProcManProcessProcData *procData, ProcManExitStatus *exitStatus) {
	uint16_t syscallId=procData->regs[0];
	switch(syscallId) {
		case BytecodeSyscallIdExit:
			*exitStatus=procData->regs[1];
			kernelLog(LogTypeInfo, kstrP("exit syscall from process %u (%s), status %u, updating state\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), *exitStatus);
			process->state=ProcManProcessStateExiting;
			return false;
		break;
		case BytecodeSyscallIdGetPid:
			procData->regs[0]=procManGetPidFromProcess(process);
			return true;
		break;
		case BytecodeSyscallIdGetArgC:
			procData->regs[0]=procData->argc;
			return true;
		break;
		case BytecodeSyscallIdGetArgVN: {
			uint8_t n=procData->regs[1];
			BytecodeWord bufAddr=procData->regs[2];
			// TODO: Use this: BytecodeWord bufLen=procData->regs[3];

			if (n>=procData->argc)
				procData->regs[0]=0;
			else {
				char arg[ProcManArgLenMax];
				if (!procManProcessGetArgvN(process, procData, n, arg)) {
					kernelLog(LogTypeWarning, kstrP("failed during getargvn syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				if (!procManProcessMemoryWriteStr(process, procData, bufAddr, arg)) {
					kernelLog(LogTypeWarning, kstrP("failed during getargvn syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				procData->regs[0]=strlen(arg);
			}

			return true;
		} break;
		case BytecodeSyscallIdFork:
			procManProcessFork(process, procData);
			return true;
		break;
		case BytecodeSyscallIdExec:
			if (!procManProcessExec(process, procData)) {
				kernelLog(LogTypeWarning, kstrP("failed during exec syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		break;
		case BytecodeSyscallIdWaitPid: {
			BytecodeWord waitPid=procData->regs[1];
			BytecodeWord timeout=procData->regs[2];

			// If given pid does not represent a process, return immediately
			if (procManGetProcessByPid(waitPid)==NULL) {
				procData->regs[0]=ProcManExitStatusNoProcess;
			} else {
				// Otherwise indicate process is waiting for this pid to die
				process->state=ProcManProcessStateWaitingWaitpid;
				process->waitingData8=waitPid;
				process->waitingData16=(timeout>0 ? (ktimeGetMs()+999)/1000+timeout : 0); // +999 is to make sure we do not sleep for less than the given number of seconds (as we would if we round the result of millis down)
			}

			return true;
		} break;
		case BytecodeSyscallIdGetPidPath: {
			ProcManPid pid=procData->regs[1];
			BytecodeWord bufAddr=procData->regs[2];

			ProcManProcess *qProcess=procManGetProcessByPid(pid);
			if (qProcess!=NULL) {
				const char *execPath=procManGetExecPathFromProcess(qProcess);
				if (!procManProcessMemoryWriteStr(process, procData, bufAddr, execPath)) {
					kernelLog(LogTypeWarning, kstrP("failed during getpidpath syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				procData->regs[0]=1;
			} else
				procData->regs[0]=0;

			return true;
		} break;
		case BytecodeSyscallIdGetPidState: {
			ProcManPid pid=procData->regs[1];
			BytecodeWord bufAddr=procData->regs[2];

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
					kernelLog(LogTypeWarning, kstrP("failed during getpidstate syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				procData->regs[0]=1;
			} else
				procData->regs[0]=0;

			return true;
		} break;
		case BytecodeSyscallIdGetAllCpuCounts: {
			BytecodeWord bufAddr=procData->regs[1];
			for(ProcManPid i=0; i<ProcManPidMax; ++i) {
				ProcManProcess *qProcess=procManGetProcessByPid(i);
				uint16_t value;
				if (qProcess!=NULL)
					value=qProcess->instructionCounter;
				else
					value=0;
				if (!procManProcessMemoryWriteWord(process, procData, bufAddr, value)) {
					kernelLog(LogTypeWarning, kstrP("failed during getallcpucounts syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				bufAddr+=2;
			}

			return true;
		} break;
		case BytecodeSyscallIdKill: {
			ProcManPid pid=procData->regs[1];
			kernelLog(LogTypeInfo, kstrP("process %u - kill syscall directed at %u\n"), procManGetPidFromProcess(process), pid);
			if (pid!=0) // do not allow killing init
				procManProcessKill(pid, ProcManExitStatusKilled, (pid==procManGetPidFromProcess(process) ? procData : NULL));

			return true;
		} break;
		case BytecodeSyscallIdSignal: {
			ProcManPid targetPid=procData->regs[1];
			BytecodeSignalId signalId=procData->regs[2];
			if (signalId<BytecodeSignalIdNB) {
				if (targetPid==0 && signalId==BytecodeSignalIdSuicide)
					kernelLog(LogTypeWarning, kstrP("process %u - warning cannot send signal 'suicide' to init (target pid=%u)\n"), procManGetPidFromProcess(process), targetPid);
				else
					procManProcessSendSignal(targetPid, signalId);
			} else
				kernelLog(LogTypeWarning, kstrP("process %u - warning bad signalId %u in signal syscall (target pid=%u)\n"), procManGetPidFromProcess(process), signalId, targetPid);

			return true;
		} break;
		case BytecodeSyscallIdGetPidFdN: {
			// Grab arguments
			ProcManPid pid=procData->regs[1];
			BytecodeWord n=procData->regs[2];

			// Bad pid or n?
			if (pid>=ProcManPidMax || n>=ProcManMaxFds) {
				procData->regs[0]=KernelFsFdInvalid;
				return true;
			}

			// Load/copy open fds table
			KernelFsFd fds[ProcManMaxFds];
			if (pid==procManGetPidFromProcess(process))
				memcpy(fds, procData->fds, sizeof(procData->fds));
			else if (!procManProcessGetOpenFds(pid, fds)) {
				procData->regs[0]=KernelFsFdInvalid;
				return true;
			}

			// Read table at given index
			procData->regs[0]=fds[n];

			return true;
		} break;
		case BytecodeSyscallIdExec2: {
			if (!procManProcessExec2(process, procData)) {
				kernelLog(LogTypeWarning, kstrP("failed during exec2 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdGetPidRam: {
			BytecodeWord pid=procData->regs[1];
			ProcManProcess *qProcess=procManGetProcessByPid(pid);
			if (qProcess!=NULL) {
				uint8_t qEnvVarDataLen;
				uint16_t qRamLen;
				if (!procManProcessLoadProcDataEnvVarDataLen(qProcess, &qEnvVarDataLen) ||
				    !procManProcessLoadProcDataRamLen(qProcess, &qRamLen)) {
					kernelLog(LogTypeWarning, kstrP("process %u getpid %u - could not load q proc fields\n"), pid, procManGetPidFromProcess(qProcess));
					procData->regs[0]=0;
				} else
					procData->regs[0]=sizeof(ProcManProcessProcData)+qEnvVarDataLen+qRamLen;
			} else
				procData->regs[0]=0;

			return true;
		} break;
		case BytecodeSyscallIdRead: {
			KernelFsFd fd=procData->regs[1];

			// Check for bad fd
			if (!kernelFsFileIsOpenByFd(fd)) {
				procData->regs[0]=0;
				return true;
			}

			// Check if would block
			if (!kernelFsFileCanRead(fd)) {
				// Reading would block - so enter waiting state until data becomes available.
				process->state=ProcManProcessStateWaitingRead;
				process->waitingData8=fd;
			} else {
				// Otherwise read as normal (stopping before we would block)
				if (!procManProcessRead(process, procData)) {
					kernelLog(LogTypeWarning, kstrP("failed during read syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
			}

			return true;
		} break;
		case BytecodeSyscallIdWrite: {
			KernelFsFd fd=procData->regs[1];
			uint16_t offset=procData->regs[2];
			uint16_t bufAddr=procData->regs[3];
			KernelFsFileOffset len=procData->regs[4];

			// Check for bad fd
			if (!kernelFsFileIsOpenByFd(fd)) {
				procData->regs[0]=0;
				return true;
			}

			// Write block at a time
			KernelFsFileOffset i=0;
			while(i<len) {
				KernelFsFileOffset chunkSize=len-i;
				if (chunkSize>256)
					chunkSize=256;

				if (!procManProcessMemoryReadBlock(process, procData, bufAddr+i, (uint8_t *)procManScratchBuf256, chunkSize, true)) {
					kernelLog(LogTypeWarning, kstrP("failed during write syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				KernelFsFileOffset written=kernelFsFileWriteOffset(fd, offset+i, (const uint8_t *)procManScratchBuf256, chunkSize);
				i+=written;
				if (written<chunkSize)
					break;
			}
			procData->regs[0]=i;

			return true;
		} break;
		case BytecodeSyscallIdOpen: {
			// Ensure process has a spare slot in the fds table
			unsigned fdsIndex;
			for(fdsIndex=0; fdsIndex<ProcManMaxFds; ++fdsIndex)
				if (procData->fds[fdsIndex]==KernelFsFdInvalid)
					break;

			if (fdsIndex==ProcManMaxFds) {
				kernelLog(LogTypeWarning, kstrP("failed during open syscall, fds table full, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=KernelFsFdInvalid;
				return true;
			}

			// Read path from user space
			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during open syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			// Attempt to open file (and if successful add to fds table)
			procData->regs[0]=kernelFsFileOpen(path);
			procData->fds[fdsIndex]=procData->regs[0];

			// Write to log
			kernelLog(LogTypeInfo, kstrP("open syscall: path='%s' fd=%u, fdsindex=%u, process %u (%s)\n"), path, procData->regs[0], fdsIndex, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));

			return true;
		} break;
		case BytecodeSyscallIdClose: {
			// Grab path first for logging, before we close the file and lose it.
			KStr pathKStr=kernelFsGetFilePath(procData->regs[1]);
			char path[KernelFsPathMax];
			kstrStrcpy(path, pathKStr);

			// Close file
			kernelFsFileClose(procData->regs[1]);

			// Write to log
			if (kstrIsNull(pathKStr))
				kernelLog(LogTypeInfo, kstrP("close syscall: fd=%u, nopath, process %u (%s)\n"), procData->regs[1], procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			else
				kernelLog(LogTypeInfo, kstrP("close syscall: fd=%u, path='%s', process %u (%s)\n"), procData->regs[1], path, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));

			// Remove from fds table.
			// Note: we remove exactly one occurence to match open syscall.
			unsigned i;
			for(i=0; i<ProcManMaxFds; ++i)
				if (procData->fds[i]==procData->regs[1]) {
					procData->fds[i]=KernelFsFdInvalid;
					kernelLog(LogTypeInfo, kstrP("close syscall: clearing fdsindex=%u (fd=%u), process %u (%s)\n"), i, procData->regs[1], procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					break;
				}
			if (i==ProcManMaxFds)
				kernelLog(LogTypeWarning, kstrP("close syscall, fd %u not found in process fds table, process %u (%s)\n"), procData->regs[1] ,procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));

			return true;
		} break;
		case BytecodeSyscallIdDirGetChildN: {
			KernelFsFd fd=procData->regs[1];
			BytecodeWord childNum=procData->regs[2];
			uint16_t bufAddr=procData->regs[3];

			char childPath[KernelFsPathMax];
			bool result=kernelFsDirectoryGetChild(fd, childNum, childPath);

			if (result) {
				if (!procManProcessMemoryWriteStr(process, procData, bufAddr, childPath)) {
					kernelLog(LogTypeWarning, kstrP("failed during dirgetchildn syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				procData->regs[0]=1;
			} else {
				procData->regs[0]=0;
			}

			return true;
		} break;
		case BytecodeSyscallIdGetPath: {
			KernelFsFd fd=procData->regs[1];
			uint16_t bufAddr=procData->regs[2];

			const char *srcPath=NULL;
			if (!kstrIsNull(kernelFsGetFilePath(fd))) {
				kstrStrcpy(procManScratchBufPath2, kernelFsGetFilePath(fd));
				srcPath=procManScratchBufPath2;
			}
			if (srcPath==NULL)
				procData->regs[0]=0;
			else {
				if (!procManProcessMemoryWriteStr(process, procData, bufAddr, srcPath)) {
					kernelLog(LogTypeWarning, kstrP("failed during getpath syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				procData->regs[0]=1;
			}

			return true;
		} break;
		case BytecodeSyscallIdResizeFile: {
			// Grab path and new size
			uint16_t pathAddr=procData->regs[1];
			KernelFsFileOffset newSize=procData->regs[2];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
					kernelLog(LogTypeWarning, kstrP("failed during resizefile syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			// Resize (or create if does not exist)
			if (!kernelFsPathIsValid(path))
				procData->regs[0]=0;
			else if (kernelFsFileExists(path))
				procData->regs[0]=kernelFsFileResize(path, newSize);
			else
				procData->regs[0]=kernelFsFileCreateWithSize(path, newSize);

			return true;
		} break;
		case BytecodeSyscallIdFileGetLen: {
			uint16_t pathAddr=procData->regs[1];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during filegetlen syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);
			procData->regs[0]=(kernelFsPathIsValid(path) ? kernelFsFileGetLen(path) : 0);

			return true;
		} break;
		case BytecodeSyscallIdTryReadByte: {
			KernelFsFd fd=procData->regs[1];

			// Check for bad fd
			if (!kernelFsFileIsOpenByFd(fd)) {
				procData->regs[0]=256;
				return true;
			}

			// Save terminal settings and change to avoid waiting for newline
			#ifdef ARDUINO
			bool oldBlocking=kernelDevTtyS0BlockingFlag;
			kernelDevTtyS0BlockingFlag=false;
			#else
			static struct termios termOld, termNew;
			tcgetattr(STDIN_FILENO, &termOld);

			termNew=termOld;
			termNew.c_lflag&=~ICANON;
			tcsetattr(STDIN_FILENO, TCSANOW, &termNew);
			#endif

			// Attempt to read
			uint8_t value;
			KernelFsFileOffset readResult=kernelFsFileReadOffset(fd, 0, &value, 1, false);

			// Restore terminal settings
			#ifdef ARDUINO
			kernelDevTtyS0BlockingFlag=oldBlocking;
			#else
			tcsetattr(STDIN_FILENO, TCSANOW, &termOld);
			#endif

			// Set result in r0
			if (readResult==1)
				procData->regs[0]=value;
			else
				procData->regs[0]=256;

			return true;
		} break;
		case BytecodeSyscallIdIsDir: {
			uint16_t pathAddr=procData->regs[1];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during isdir syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);
			procData->regs[0]=kernelFsFileIsDir(path);

			return true;
		} break;
		case BytecodeSyscallIdFileExists: {
			uint16_t pathAddr=procData->regs[1];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during fileexists syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);
			procData->regs[0]=(kernelFsPathIsValid(path) ? kernelFsFileExists(path) : false);

			return true;
		} break;
		case BytecodeSyscallIdDelete: {
			uint16_t pathAddr=procData->regs[1];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during delete syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);
			procData->regs[0]=(kernelFsPathIsValid(path) ? kernelFsFileDelete(path) : false);

			return true;
		} break;
		case BytecodeSyscallIdEnvGetStdinFd:
			procData->regs[0]=procData->stdinFd;
			return true;
		break;
		case BytecodeSyscallIdEnvSetStdinFd:
			procData->stdinFd=procData->regs[1];
			return true;
		break;
		case BytecodeSyscallIdEnvGetStdoutFd:
			procData->regs[0]=procData->stdoutFd;
			return true;
		break;
		case BytecodeSyscallIdEnvSetStdoutFd:
			procData->stdoutFd=procData->regs[1];
			return true;
		break;
		case BytecodeSyscallIdEnvGetPwd: {
			char pwd[KernelFsPathMax];
			if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->pwd, pwd, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during envgetpwd syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(pwd);

			if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], pwd)) {
				kernelLog(LogTypeWarning, kstrP("failed during envgetpwd syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdEnvSetPwd: {
			BytecodeWord addr=procData->regs[1];
			if (addr<BytecodeMemoryRamAddr) {
				kernelLog(LogTypeWarning, kstrP("failed during envsetpwd syscall - addr 0x%04X does not point to RW region, process %u (%s), killing\n"), addr, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			KernelFsFileOffset ramIndex=addr-BytecodeMemoryRamAddr;
			procData->pwd=ramIndex+procData->envVarDataLen;

			return true;
		} break;
		case BytecodeSyscallIdEnvGetPath: {
			char path[ProcManEnvVarPathMax];
			if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->path, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during envgetpath syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], path)) {
				kernelLog(LogTypeWarning, kstrP("failed during envgetpath syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdEnvSetPath: {
			BytecodeWord addr=procData->regs[1];
			if (addr<BytecodeMemoryRamAddr) {
				kernelLog(LogTypeWarning, kstrP("failed during envsetpath syscall - addr 0x%04X does not point to RW region, process %u (%s), killing\n"), addr, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			KernelFsFileOffset ramIndex=addr-BytecodeMemoryRamAddr;
			procData->path=ramIndex+procData->envVarDataLen;

			return true;
		} break;
		case BytecodeSyscallIdTimeMonotonic:
			procData->regs[0]=(ktimeGetMs()/1000);

			return true;
		break;
		case BytecodeSyscallIdRegisterSignalHandler: {
			uint16_t signalId=procData->regs[1];
			uint16_t handlerAddr=procData->regs[2];

			if (signalId>=BytecodeSignalIdNB) {
				kernelLog(LogTypeWarning, kstrP("process %u (%s), tried to register handler for invalid signal %u\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), signalId);
			} else if (handlerAddr>=256) {
				kernelLog(LogTypeWarning, kstrP("process %u (%s), tried to register handler for signal %u, but handler addr %u is out of range (must be less than 256)\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), signalId, handlerAddr);
			} else
				procData->signalHandlers[signalId]=handlerAddr;

			return true;
		} break;
		case BytecodeSyscallIdShutdown:
			// Kill all processes, causing kernel to return/halt
			kernelLog(LogTypeInfo, kstrP("process %u (%s) initiated shutdown via syscall\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			kernelShutdownBegin();

			return true;
		break;
		case BytecodeSyscallIdMount: {
			// Grab arguments
			uint16_t format=procData->regs[1];
			uint16_t devicePathAddr=procData->regs[2];
			uint16_t dirPathAddr=procData->regs[3];

			char devicePath[KernelFsPathMax], dirPath[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, devicePathAddr, devicePath, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during mount syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			if (!procManProcessMemoryReadStr(process, procData, dirPathAddr, dirPath, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during mount syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(devicePath);
			kernelFsPathNormalise(dirPath);

			// Attempt to mount
			procData->regs[0]=kernelMount(format, devicePath, dirPath);

			return true;
		} break;
		case BytecodeSyscallIdUnmount: {
			// Grab arguments
			uint16_t devicePathAddr=procData->regs[1];

			char devicePath[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, devicePathAddr, devicePath, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during mount syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(devicePath);

			// Unmount
			kernelUnmount(devicePath);

			return true;
		} break;
		case BytecodeSyscallIdIoctl: {
			// Grab arguments
			uint16_t fd=procData->regs[1];
			uint16_t command=procData->regs[2];
			uint16_t data=procData->regs[3];

			// Invalid fd?
			KStr kstrPath=kernelFsGetFilePath(fd);
			if (kstrIsNull(kstrPath)) {
				kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall fd %u, process %u (%s)\n"), fd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			} else {
				kstrStrcpy(procManScratchBufPath0, kstrPath);

				// Check for stdin/stdout terminal
				if (strcmp(procManScratchBufPath0, "/dev/ttyS0")==0) {
					switch(command) {
						case BytecodeSyscallIdIoctlCommandDevTtyS0SetEcho: {
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
							kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall command %u (on fd %u, device '%s'), process %u (%s)\n"), command, fd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						break;
					}
				} else if (strncmp(procManScratchBufPath0, "/dev/pin", 8)==0) {
					// Check for pin device file path
					uint8_t pinNum=atoi(procManScratchBufPath0+8); // TODO: Verify valid number (e.g. currently '/dev/pin' will operate pin0 (although the file /dev/pin must exist so should be fine for now)
					switch(command) {
						case BytecodeSyscallIdIoctlCommandDevPinSetMode: {
							// Forbid mode changes to SPI bus pins.
							if (spiIsReservedPin(pinNum)) {
								kernelLog(LogTypeWarning, kstrP("ioctl attempting to set mode of SPI bus pin %u (on fd %u, device '%s'), process %u (%s)\n"), pinNum, fd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								break;
							}

							// Forbid mode changes from user space to SPI device pins (even if associated device has type HwDeviceTypeRaw).
							HwDeviceId hwDeviceId=hwDeviceGetDeviceForPin(pinNum);
							if (hwDeviceId!=HwDeviceIdMax) {
								kernelLog(LogTypeWarning, kstrP("ioctl attempting to set mode of SPI device pin %u (on fd %u, device '%s'), process %u (%s)\n"), pinNum, fd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
								break;
							}

							// Set pin mode
							pinSetMode(pinNum, (data==PinModeInput ? PinModeInput : PinModeOutput));
						} break;
						default:
							kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall command %u (on fd %u, device '%s'), process %u (%s)\n"), command, fd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						break;
					}
				} else {
					kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall device (fd %u, device '%s'), process %u (%s)\n"), fd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				}
			}
			return true;
		} break;
		case BytecodeSyscallIdGetLogLevel:
			procData->regs[0]=kernelLogGetLevel();
			return true;
		break;
		case BytecodeSyscallIdSetLogLevel:
			kernelLogSetLevel((procData->regs[1]<=LogLevelNone) ? procData->regs[1] : LogLevelNone);
			return true;
		break;
		case BytecodeSyscallIdStrchr: {
			uint16_t strAddr=procData->regs[1];
			uint16_t c=procData->regs[2];

			procData->regs[0]=0;
			while(1) {
				uint8_t value;
				if (!procManProcessMemoryReadByte(process, procData, strAddr, &value)) {
					kernelLog(LogTypeWarning, kstrP("failed during strchr syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}

				if (value==c) {
					procData->regs[0]=strAddr;
					break;
				}
				if (value=='\0')
					break;

				strAddr++;
			}
			return true;
		} break;
		case BytecodeSyscallIdStrchrnul: {
			uint16_t strAddr=procData->regs[1];
			uint16_t c=procData->regs[2];

			procData->regs[0]=strAddr;
			while(1) {
				uint8_t value;
				if (!procManProcessMemoryReadByte(process, procData, procData->regs[0], &value)) {
					kernelLog(LogTypeWarning, kstrP("failed during strchrnul syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}

				if (value==c)
					break;
				if (value=='\0')
					break;

				++procData->regs[0];
			}
			return true;
		} break;
		case BytecodeSyscallIdMemmove: {
			uint16_t destAddr=procData->regs[1];
			uint16_t srcAddr=procData->regs[2];
			uint16_t size=procData->regs[3];

			if (destAddr<srcAddr) {
				uint16_t i=0;
				while(i<size) {
					KernelFsFileOffset chunkSize=size-i;
					if (chunkSize>256)
						chunkSize=256;

					if (!procManProcessMemoryReadBlock(process, procData, srcAddr+i, (uint8_t *)procManScratchBuf256, chunkSize, true)) {
						kernelLog(LogTypeWarning, kstrP("failed during memcpy syscall reading, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					if (!procManProcessMemoryWriteBlock(process, procData, destAddr+i, (const uint8_t *)procManScratchBuf256, chunkSize)) {
						kernelLog(LogTypeWarning, kstrP("failed during memcpy syscall writing, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}

					i+=chunkSize;
				}
			} else {
				// dest>=src reverse case
				// note: brackets around (size-chunkSize) are essential due to using unsigned integers
				uint16_t i=0;
				while(i<size) {
					KernelFsFileOffset chunkSize=size-i;
					if (chunkSize>256)
						chunkSize=256;

					if (!procManProcessMemoryReadBlock(process, procData, srcAddr+(size-chunkSize)-i, (uint8_t *)procManScratchBuf256, chunkSize, true)) {
						kernelLog(LogTypeWarning, kstrP("failed during memcpy syscall reading, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}
					if (!procManProcessMemoryWriteBlock(process, procData, destAddr+(size-chunkSize)-i, (const uint8_t *)procManScratchBuf256, chunkSize)) {
						kernelLog(LogTypeWarning, kstrP("failed during memcpy syscall writing, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
						return false;
					}

					i+=chunkSize;
				}
			}
			return true;
		} break;
		case BytecodeSyscallIdHwDeviceRegister: {
			HwDeviceId id=procData->regs[1];
			HwDeviceType type=procData->regs[2];

			procData->regs[0]=hwDeviceRegister(id, type);

			return true;
		} break;
		case BytecodeSyscallIdHwDeviceDeregister: {
			HwDeviceId id=procData->regs[1];

			hwDeviceDeregister(id);

			return true;
		} break;
		case BytecodeSyscallIdHwDeviceGetType: {
			HwDeviceId id=procData->regs[1];

			procData->regs[0]=hwDeviceGetType(id);

			return true;
		} break;
		case BytecodeSyscallIdHwDeviceSdCardReaderMount: {
			// Grab arguments
			HwDeviceId id=procData->regs[1];
			uint16_t mountPointAddr=procData->regs[2];

			char mountPoint[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, mountPointAddr, mountPoint, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during spidevicesdcardreadermount syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(mountPoint);

			// Attempt to mount
			procData->regs[0]=hwDeviceSdCardReaderMount(id, mountPoint);

			return true;
		} break;
		case BytecodeSyscallIdHwDeviceSdCardReaderUnmount: {
			HwDeviceId id=procData->regs[1];

			hwDeviceSdCardReaderUnmount(id);

			return true;
		} break;
		case BytecodeSyscallIdHwDeviceDht22GetTemperature: {
			HwDeviceId id=procData->regs[1];

			procData->regs[0]=hwDeviceDht22GetTemperature(id);

			return true;
		} break;
		case BytecodeSyscallIdHwDeviceDht22GetHumidity: {
			HwDeviceId id=procData->regs[1];

			procData->regs[0]=hwDeviceDht22GetHumidity(id);

			return true;
		} break;
	}

	kernelLog(LogTypeWarning, kstrP("invalid syscall id=%i, process %u (%s), killing\n"), syscallId, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	return false;
}

void procManProcessFork(ProcManProcess *parent, ProcManProcessProcData *procData) {
#define childProcPath procManScratchBufPath0
#define childRamPath procManScratchBufPath1
	KernelFsFd childRamFd=KernelFsFdInvalid;
	ProcManPid parentPid=procManGetPidFromProcess(parent);

	kernelLog(LogTypeInfo, kstrP("fork request from process %u\n"), parentPid);

	// Find a PID for the new process
	ProcManPid childPid=procManFindUnusedPid();
	if (childPid==ProcManPidMax) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - no spare PIDs\n"), parentPid);
		goto error;
	}
	ProcManProcess *child=&(procManData.processes[childPid]);

	// Construct proc file path
	sprintf(childProcPath, "/tmp/proc%u", childPid);
	sprintf(childRamPath, "/tmp/ram%u", childPid);

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(childProcPath, sizeof(ProcManProcessProcData))) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not create child process data file at '%s' of size %u\n"), parentPid, childProcPath, sizeof(procManProcessLoadProcData));
		goto error;
	}
	uint16_t ramTotalSize=procData->envVarDataLen+procData->ramLen;
	if (!kernelFsFileCreateWithSize(childRamPath, ramTotalSize)) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not create child ram data file at '%s' of size %u\n"), parentPid, childRamPath, ramTotalSize);
		goto error;
	}

	// Attempt to open proc and ram files
	child->procFd=kernelFsFileOpen(childProcPath);
	if (child->procFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not open child process data file at '%s'\n"), parentPid, childProcPath);
		goto error;
	}

	childRamFd=kernelFsFileOpen(childRamPath);
	if (childRamFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not open child ram data file at '%s'\n"), parentPid, childRamPath);
		goto error;
	}

	// Simply use same FD as parent for the program data
	child->state=ProcManProcessStateActive;
	child->progmemFd=procManData.processes[parentPid].progmemFd;
	child->instructionCounter=0;

	// Initialise proc file
	KernelFsFd savedFd=procData->ramFd;
	BytecodeWord savedR0=procData->regs[0];
	memcpy(procManScratchBuf256, procData->fds, sizeof(procData->fds));

	procData->ramFd=childRamFd;
	procData->regs[0]=0; // indicate success in the child
	for(unsigned i=0; i<ProcManMaxFds; ++i)
		procData->fds[i]=KernelFsFdInvalid;
	bool storeRes=procManProcessStoreProcData(child, procData);
	procData->ramFd=savedFd;
	procData->regs[0]=savedR0;
	memcpy(procData->fds, procManScratchBuf256, sizeof(procData->fds));
	if (!storeRes) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not save child process data file to '%s'\n"), parentPid, childProcPath);
		goto error;
	}

	// Copy parent's ram into child's
	// Use a scratch buffer to copy up to 256 bytes at a time.
	KernelFsFileOffset i;
	for(i=0; i+255<ramTotalSize; i+=256) {
		if (kernelFsFileReadOffset(procData->ramFd, i, (uint8_t *)procManScratchBuf256, 256, false)!=256 ||
		    kernelFsFileWriteOffset(childRamFd, i, (const uint8_t *)procManScratchBuf256, 256)!=256) {
			kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not copy parent's RAM into child's (managed %u/%u)\n"), parentPid, i, ramTotalSize);
			goto error;
		}
	}
	for(; i<ramTotalSize; i++) {
		uint8_t value;
		if (kernelFsFileReadOffset(procData->ramFd, i, &value, 1, false)!=1 ||
		    kernelFsFileWriteOffset(childRamFd, i, &value, 1)!=1) {
			kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not copy parent's RAM into child's (managed %u/%u)\n"), parentPid, i, ramTotalSize);
			goto error;
		}
	}

	// Update parent return value with child's PID
	procData->regs[0]=childPid;

	kernelLog(LogTypeInfo, kstrP("forked from %u, creating child %u\n"), parentPid, childPid);

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
#undef childProcPath
#undef childRamPath
}

bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData) {
#define argv ((char *)procManScratchBuf256)

	// Grab arg and write to log
	uint8_t argc=procData->regs[1];
	kernelLog(LogTypeInfo, kstrP("exec in %u - argc=%u\n"), procManGetPidFromProcess(process), argc);

	if (argc<1) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - no arguments\n"), procManGetPidFromProcess(process));
		return false;
	}

	// Create argv string by reading argv from user space one arg at a time
	KernelFsFileOffset argvPtr=procData->regs[2];
	int argvTotalSize=0;
	for(int i=0; i<argc; ++i) {
		if (!procManProcessMemoryReadStr(process, procData, argvPtr+argvTotalSize, argv+argvTotalSize, KernelFsPathMax)) {
			kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not read argv string\n"), procManGetPidFromProcess(process));
			return false;
		}
		argvTotalSize+=strlen(argv+argvTotalSize)+1;
	}
	assert(argvTotalSize==procManArgvStringGetTotalSize(argc, argv));

	// Call common function to do rest of the work
	return procManProcessExecCommon(process, procData, argc, argv);

#undef argv
}

bool procManProcessExec2(ProcManProcess *process, ProcManProcessProcData *procData) {
#define argv ((char *)procManScratchBuf256)

	uint8_t start=procData->regs[1];
	uint8_t argc=procData->regs[2];

	// Invalid values given?
	if (start>=procData->argc || argc<1) {
		kernelLog(LogTypeInfo, kstrP("exec in %u failed - bad start/count arguments (%u,%u)\n"), procManGetPidFromProcess(process), start, argc);
		return true;
	}

	// Clamp argc if needed
	if (start+argc>procData->argc)
		argc=procData->argc-start;
	assert(argc>0);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("exec in %u - argc=%u\n"), procManGetPidFromProcess(process), argc);

	// Create argv string by reading existing kernel space argv string
	KernelFsFileOffset argvOffset=0;
	for(unsigned i=0; i<argc; ++i) {
		if (!procManProcessGetArgvN(process, procData, start+i, argv+argvOffset)) {
			kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not read argv[%u]\n"), procManGetPidFromProcess(process), start+i);
			return false;
		}

		argvOffset+=strlen(argv+argvOffset)+1;
	}

	// Call common function to do rest of the work
	return procManProcessExecCommon(process, procData, argc, argv);

#undef argv
}

bool procManProcessExecCommon(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t argc, char *argv) {
#define tempPwd procManScratchBufPath0
#define tempPath procManScratchBufPath1
#define ramPath procManScratchBufPath2

	// Write to log
	kernelLog(LogTypeInfo, kstrP("exec in %u - argv:\n"), procManGetPidFromProcess(process), argc);
	for(int i=0; i<argc; ++i) {
		const char *arg=procManArgvStringGetArgN(argc, argv, i);
		kernelLog(LogTypeInfo, kstrP("	%u = '%s'\n"), i, arg);
	}

	// Grab pwd and path env vars as these may now point into general ram, which is about to be cleared when we resize
	if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->pwd, tempPwd, KernelFsPathMax)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not read env var pwd at offset %u\n"), procManGetPidFromProcess(process), procData->pwd);
		return false;
	}
	kernelFsPathNormalise(tempPwd);

	if (!procManProcessMemoryReadStrAtRamfileOffset(process, procData, procData->path, tempPath, KernelFsPathMax)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not read env var path at offset %u\n"), procManGetPidFromProcess(process), procData->path);
		return false;
	}
	kernelFsPathNormalise(tempPath);

	// Load program (handling magic bytes)
	bool pathSearchFlag=procData->regs[3];
	KernelFsFd newProgmemFd=procManProcessLoadProgmemFile(process, &argc, argv, (pathSearchFlag ? tempPath : NULL), (pathSearchFlag ? tempPwd : NULL));
	if (newProgmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not open progmem file ('%s')\n"), procManGetPidFromProcess(process), argv);
		return true;
	}

	int argvTotalSize=procManArgvStringGetTotalSize(argc, argv);

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
	procData->regs[BytecodeRegisterIP]=0;

	// Update env var array and calculate new len
	procData->envVarDataLen=0;

	procData->pwd=procData->envVarDataLen;
	procData->envVarDataLen+=strlen(tempPwd)+1;
	procData->path=procData->envVarDataLen;
	procData->envVarDataLen+=strlen(tempPath)+1;

	procData->argc=argc;
	procData->argvStart=procData->envVarDataLen;
	procData->envVarDataLen+=argvTotalSize;

	// Resize ram file (clear data and add new arguments)
	procData->ramLen=0; // no ram needed initially
	uint16_t newRamTotalSize=procData->envVarDataLen+procData->ramLen;

	sprintf(ramPath, "/tmp/ram%u", pid);

	kernelFsFileClose(procData->ramFd);
	if (!kernelFsFileResize(ramPath, newRamTotalSize)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not resize new processes RAM file at '%s' to %u\n"), procManGetPidFromProcess(process), ramPath, newRamTotalSize);
		return false;
	}
	procData->ramFd=kernelFsFileOpen(ramPath);
	assert(procData->ramFd!=KernelFsFdInvalid);

	// Write env vars into ram file
	if (kernelFsFileWriteOffset(procData->ramFd, procData->pwd, (const uint8_t *)(tempPwd), strlen(tempPwd)+1)!=strlen(tempPwd)+1) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not write env var pwd into new processes memory\n"), procManGetPidFromProcess(process));
		return false;
	}

	if (kernelFsFileWriteOffset(procData->ramFd, procData->path, (const uint8_t *)(tempPath), strlen(tempPath)+1)!=strlen(tempPath)+1) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not write env var path into new processes memory\n"), procManGetPidFromProcess(process));
		return false;
	}

	if (kernelFsFileWriteOffset(procData->ramFd, procData->argvStart, (const uint8_t *)(argv), argvTotalSize)!=argvTotalSize) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not write argv into new processes memory\n"), procManGetPidFromProcess(process));
		return false;
	}

	// Clear any registered signal handlers.
	for(uint16_t i=0; i<BytecodeSignalIdNB; ++i)
		procData->signalHandlers[i]=ProcManSignalHandlerInvalid;

	// Save proc data
	if (!procManProcessStoreProcData(process, procData)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not store procdata\n"), procManGetPidFromProcess(process));
		return false;
	}

	kernelLog(LogTypeInfo, kstrP("exec in %u succeeded\n"), procManGetPidFromProcess(process));

	return true;

#undef tempPwd
#undef tempPath
#undef ramPath
}

KernelFsFd procManProcessLoadProgmemFile(ProcManProcess *process, uint8_t *argc, char *argvStart, const char *envPath, const char *envPwd) {
	assert(process!=NULL);

	// Grab a copy of the exec path so we can modify it
	char originalExecPath[KernelFsPathMax];
	strcpy(originalExecPath, argvStart);

	// Normalise path
	kernelFsPathNormalise(originalExecPath);

	// If no slashes and asked, Search through PATH for matching executables.
	if (envPath!=NULL && strchr(originalExecPath, '/')==NULL) {
		const char *envPathPtr=envPath;
		while(1) {
			// End of PATH string?
			if (*envPathPtr=='\0')
				break;

			// Grab next directory part from PATH string
			const char *colonPtr=strchr(envPathPtr, ':');
			if (colonPtr!=NULL) {
				memcpy(originalExecPath, envPathPtr, colonPtr-envPathPtr);
				originalExecPath[colonPtr-envPathPtr]='\0';
				envPathPtr+=colonPtr-envPathPtr+1;
			} else {
				strcpy(originalExecPath, envPathPtr);
				envPathPtr+=strlen(originalExecPath);
			}
			if (strlen(originalExecPath)==0)
				continue;

			// Add slash and given name
			strcat(originalExecPath, "/");
			strcat(originalExecPath, argvStart);

			// Noramlise and check for existence
			kernelFsPathNormalise(originalExecPath);
			if (kernelFsFileExists(originalExecPath))
				break;

			// Restore originalExecPath
			strcpy(originalExecPath, argvStart);
			kernelFsPathNormalise(originalExecPath);
		}
	}

	// If no slash at start and asked, make path absolute.
	if (envPwd!=NULL && originalExecPath[0]!='/') {
		strcpy(originalExecPath, envPwd);
		strcat(originalExecPath, "/");
		strcat(originalExecPath, argvStart); // argvStart has not been modified yet so we can use it
		kernelFsPathNormalise(originalExecPath);
	}

	// Check path is valid
	if (!kernelFsPathIsValid(originalExecPath)) {
		kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - path '%s' not valid\n"), procManGetPidFromProcess(process), originalExecPath);
		return KernelFsFdInvalid;
	}

	if (kernelFsFileIsDir(originalExecPath)) {
		kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - path '%s' is a directory\n"), procManGetPidFromProcess(process), originalExecPath);
		return KernelFsFdInvalid;
	}

	// Attempt to read magic bytes and handle any special logic such as the '#!' shebang syntax
	KernelFsFd newProgmemFd=KernelFsFdInvalid;
	uint8_t magicByteRecursionCount, magicByteRecursionCountMax=8;
	for(magicByteRecursionCount=0; magicByteRecursionCount<magicByteRecursionCountMax; ++magicByteRecursionCount) {
		const char *loopExecFile=(magicByteRecursionCount>0 ? argvStart : originalExecPath);

		// Attempt to open program file
		newProgmemFd=kernelFsFileOpen(loopExecFile);
		if (newProgmemFd==KernelFsFdInvalid) {
			kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - could not open program at '%s'\n"), procManGetPidFromProcess(process), loopExecFile);
			return KernelFsFdInvalid;
		}

		// Read first two bytes to decide how to execute
		uint8_t magicBytes[2];
		if (kernelFsFileRead(newProgmemFd, magicBytes, 2)!=2) {
			kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - could not read 2 magic bytes at start of '%s', fd %u\n"), procManGetPidFromProcess(process), loopExecFile, newProgmemFd);
			kernelFsFileClose(newProgmemFd);
			return KernelFsFdInvalid;
		}

		if (magicBytes[0]==BytecodeMagicByte1 && magicBytes[1]==BytecodeMagicByte2) {
			// A standard native executable - no special handling required (the magic bytes run as harmless instructions)

			// Write to log
			kernelLog(LogTypeInfo, kstrP("loading exeutable in %u - magic bytes '//' detected, assuming native executable (original exec path '%s', fd %u)\n"), procManGetPidFromProcess(process), loopExecFile, newProgmemFd);
			break;
		} else if (magicBytes[0]=='#' && magicBytes[1]=='!') {
			// An interpreter (with path following after '#!') should be used instead to run this file.

			// Read interpreter path string
			char interpreterPath[KernelFsPathMax];
			KernelFsFileOffset readCount=kernelFsFileReadOffset(newProgmemFd, 2, (uint8_t *)interpreterPath, KernelFsPathMax, false);
			interpreterPath[readCount-1]='\0';

			// Look for newline and if found terminate string here
			char *newlinePtr=strchr(interpreterPath, '\n');
			if (newlinePtr==NULL) {
				kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - '#!' not followed by interpreter path (original exec path '%s', fd %u)\n"), procManGetPidFromProcess(process), loopExecFile, newProgmemFd);
				kernelFsFileClose(newProgmemFd);
				return KernelFsFdInvalid;
			}
			*newlinePtr='\0';

			// Write to log
			kernelFsPathNormalise(interpreterPath);
			kernelLog(LogTypeInfo, kstrP("loading exeutable in %u - magic bytes '#!' detected, using interpreter '%s' (original exec path '%s', fd %u)\n"), procManGetPidFromProcess(process), interpreterPath, loopExecFile, newProgmemFd);

			// Close the original progmem file
			kernelFsFileClose(newProgmemFd);

			// Update args - move program executable path to 2nd arg, and set interpreter path as 1st (clearing all others)
			procManArgvUpdateForInterpreter(argc, argvStart, interpreterPath);

			// Loop again in an attempt to load interpreter
		} else {
			kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - unknown magic byte sequence 0x%02X%02X at start of '%s', fd %u\n"), procManGetPidFromProcess(process), magicBytes[0], magicBytes[1], loopExecFile, newProgmemFd);
			kernelFsFileClose(newProgmemFd);
			return KernelFsFdInvalid;
		}
	}

	if (newProgmemFd==KernelFsFdInvalid || magicByteRecursionCount==magicByteRecursionCountMax) {
		kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - could not open progmem file (shebang infinite recursion perhaps?) ('%s', fd %u)\n"), procManGetPidFromProcess(process), argvStart, newProgmemFd);
		return KernelFsFdInvalid;
	}

	return newProgmemFd;
}

bool procManProcessRead(ProcManProcess *process, ProcManProcessProcData *procData) {
	KernelFsFd fd=procData->regs[1];
	uint16_t offset=procData->regs[2];
	uint16_t bufAddr=procData->regs[3];
	KernelFsFileOffset len=procData->regs[4];

	KernelFsFileOffset i=0;
	while(i<len) {
		KernelFsFileOffset chunkSize=len-i;
		if (chunkSize>256)
			chunkSize=256;

		KernelFsFileOffset read=kernelFsFileReadOffset(fd, offset+i, (uint8_t *)procManScratchBuf256, chunkSize, false);
		if (read==0)
			break;

		if (!procManProcessMemoryWriteBlock(process, procData, bufAddr+i, (const uint8_t *)procManScratchBuf256, read))
			return false;

		i+=read;
		if (read<chunkSize)
			break;
	}

	procData->regs[0]=i;

	return true;
}

void procManResetInstructionCounters(void) {
	for(ProcManPid i=0; i<ProcManPidMax; ++i)
		procManData.processes[i].instructionCounter=0;
}

char *procManArgvStringGetArgN(uint8_t argc, char *argvStart, uint8_t n) {
	// Check n is in range
	if (n>=argc)
		return NULL;

	// Look for argument by looping over all that come before it.
	while(n>0)
		n-=(*argvStart++=='\0');

	return argvStart;
}

const char *procManArgvStringGetArgNConst(uint8_t argc, const char *argvStart, uint8_t n) {
	// Check n is in range
	if (n>=argc)
		return NULL;

	// Look for argument by looping over all that come before it.
	while(n>0)
		n-=(*argvStart++=='\0');

	return argvStart;
}

int procManArgvStringGetTotalSize(uint8_t argc, const char *argvStart) {
	int len=0;
	while(argc>0)
		argc-=(argvStart[len++]=='\0');
	return len;
}

void procManArgvStringPathNormaliseArg0(uint8_t argc, char *argvStart) {
	// Ensure argc is sensible
	if (argc<1)
		return;

	// Grab total size before we lose this info after normalise call.
	int preTotalSize=procManArgvStringGetTotalSize(argc, argvStart);

	// Normalise path, making a note of the size reduction
	// We note sizes because normalise may shorten the first argument,
	// which causes issues as 2nd arg should start straight after.
	int preSize=strlen(argvStart)+1;
	kernelFsPathNormalise(argvStart);
	int postSize=strlen(argvStart)+1;
	assert(postSize<=preSize);

	// Move down all other arguments (skip if no other args or no change)
	if (argc<2 || postSize==preSize)
		return;

	memmove(argvStart+postSize, argvStart+preSize, preTotalSize-preSize);
}

void procManArgvUpdateForInterpreter(uint8_t *argc, char *argvStart, const char *interpreterPath) {
	int interpreterPathSize=strlen(interpreterPath)+1;
	int arg0Size=strlen(argvStart)+1;

	// Move original executable path to 2nd argument
	memmove(argvStart+interpreterPathSize, argvStart, arg0Size);

	// Place interpreter path in 1st argument
	memcpy(argvStart, interpreterPath, interpreterPathSize);

	// Finally adjust argc
	*argc=2;
}

void procManPrefetchDataClear(ProcManPrefetchData *pd) {
	pd->len=0;
}

bool procManPrefetchDataReadByte(ProcManPrefetchData *pd, ProcManProcess *process, ProcManProcessProcData *procData, uint16_t addr, uint8_t *value) {
	// Not already in cache?
	if (addr<pd->baseAddr || addr>=pd->baseAddr+pd->len) {
		// Attempt to read largest block we can, but try smaller sizes if this fails.
		for(pd->len=ProcManPrefetchDataBufferSize; pd->len>0; pd->len/=2)
			if (procManProcessMemoryReadBlock(process, procData, addr, pd->buffer, pd->len, false))
				break;

		// Could we not even read the requested byte?
		if (pd->len==0)
			return false;

		// At least one byte read - update base address
		pd->baseAddr=addr;
	}

	// Grab from cache
	*value=pd->buffer[addr-pd->baseAddr];
	return true;
}

void procManArgvDebug(uint8_t argc, const char *argvStart) {
	kernelLog(LogTypeInfo, kstrP("Argv Debug: argc=%u, arg0='%s'\n"), argc, argvStart);
	for(int i=1; i<argc; ++i) {
		const char *arg=procManArgvStringGetArgNConst(argc, argvStart, i);
		kernelLog(LogTypeInfo, kstrP("Argv Debug:         arg%u='%s'\n"), i, arg);
	}
}
