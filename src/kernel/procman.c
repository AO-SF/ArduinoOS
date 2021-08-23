#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef ARDUINO
#include <alloca.h>
#else
#include <libgen.h>
#include <unistd.h>
#endif

#include "hwdevice.h"
#include "kernel.h"
#include "kernelmount.h"
#include "ktime.h"
#include "log.h"
#include "pins.h"
#include "procman.h"
#include "profile.h"
#include "spi.h"
#include "tty.h"
#include "util.h"

#define procManProcessInstructionCounterMax (65500u) // largest 16 bit unsigned number, less a small safety margin
#define procManProcessInstructionsPerTick 160 // generally a higher value causes faster execution, but decreased responsiveness if many processes running
#define procManTicksPerInstructionCounterReset (procManProcessInstructionCounterMax/procManProcessInstructionsPerTick)

#define ProcManSignalHandlerInvalid 0

#define ProcManArgLenMax 64
#define ProcManEnvVarPathMax 128
#define ProcManPipeSize 128
#define ProcManMaxPipes 64

#define ProcManEnvVarsVirtualOffset ((BytecodeWord)64512u) // =63k - we map things like argv in to last 1kb of process memory

typedef enum {
	ProcManProcessStateUnused,
	ProcManProcessStateActive,
	ProcManProcessStateWaitingWaitpid,
	ProcManProcessStateWaitingRead,
	ProcManProcessStateWaitingRead32,
	ProcManProcessStateWaitingWrite,
	ProcManProcessStateWaitingWrite32,
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
	// this table stores the actual 'global' fds in use by the process (the fds the process sees in userspace are actually the indexes into this table)
	// as localfd=0 is invalid we need one less entry than it would seem
	KernelFsFd fds[ProcManMaxFds-1];

	uint8_t argc; // argument count (argv is stored with pwd and path in 'pre-space' in ram file)

	// The following fields are pointers into the the process memory space.
	uint16_t pwd; // set to '/' when init is called
	uint16_t path; // set to '/usr/games:/usr/bin:/bin:' when init is called
};

typedef struct {
	uint64_t pid:8;
	uint64_t timeoutTime:48; // set to 0 if infinite. note: we take the lowest 48 bits of the time, which is zero for the first ~8900 years after the system is booted
} ProcManProcessStateWaitingWaitpidData;

typedef struct {
	KernelFsFd globalFd;
} ProcManProcessStateWaitingReadData;

typedef struct {
	KernelFsFd globalFd;
} ProcManProcessStateWaitingRead32Data;

typedef struct {
	KernelFsFd globalFd;
} ProcManProcessStateWaitingWriteData;

typedef struct {
	KernelFsFd globalFd;
} ProcManProcessStateWaitingWrite32Data;

typedef struct {
	uint16_t instructionCounter; // reset regularly
	KernelFsFd progmemFd, procFd;
	uint8_t state;
	union {
		ProcManProcessStateWaitingWaitpidData waitingWaitpid;
		ProcManProcessStateWaitingReadData waitingRead;
		ProcManProcessStateWaitingRead32Data waitingRead32;
		ProcManProcessStateWaitingWriteData waitingWrite;
		ProcManProcessStateWaitingWrite32Data waitingWrite32;
	} stateData;
#ifndef ARDUINO
	ProfileCounter profilingCounts[BytecodeMemoryProgmemSize];
#endif
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
bool procManProcessMemoryReadDoubleWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeDoubleWord *value);
bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, char *str, uint16_t len);
bool procManProcessMemoryReadBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t *data, uint16_t len, bool verbose); // block should not cross split in memory between two types
bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t value);
bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord value);
bool procManProcessMemoryWriteDoubleWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeDoubleWord value);
bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const char *str);
bool procManProcessMemoryWriteBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const uint8_t *data, uint16_t len); // Note: addr with len should not cross over the boundary between the two parts of memory.

bool procManProcessGetArgvNAddr(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, BytecodeWord *addr); // Sets addr to 0 to indicate no such arg
bool procManProcessGetArgvNStr(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char *str);

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
bool procManProcessRead32(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessReadCommon(ProcManProcess *process, ProcManProcessProcData *procData, KernelFsFileOffset offset);

bool procManProcessWrite(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessWrite32(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessWriteCommon(ProcManProcess *process, ProcManProcessProcData *procData, KernelFsFileOffset offset);

ProcManLocalFd procManProcessOpenFile(ProcManProcess *process, ProcManProcessProcData *procData, const char *path, KernelFsFdMode mode); // attempts to open given path, if successful adds to the fd table
void procManProcessCloseFile(ProcManProcess *process, ProcManProcessProcData *procData, ProcManLocalFd localFd);
KernelFsFd procManProcessGetGlobalFdFromLocal(ProcManProcess *process, ProcManProcessProcData *procData, ProcManLocalFd localFd);
KernelFsFd procManProcessGetGlobalFdFromLocalWithPid(ProcManPid pid, ProcManLocalFd localFd); // note: information may be out of date if called from within procManProcessTick with pid of the currently running process

void procManResetInstructionCounters(void);

void procManProcessDebug(ProcManProcess *process, ProcManProcessProcData *procData);

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
	assert(programPath!=NULL);

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

	procData.argc=argc;
	assert(envVarDataLen==0); // always place argv at the start so we dont have to store an offset (given that argv can never change once after process creation, unlike the other env vars)
	int argvTotalSize=procManArgvStringGetTotalSize(argc, argvStart);
	memcpy(envVarData+envVarDataLen, argvStart, argvTotalSize);
	envVarDataLen+=argvTotalSize;

	char tempPwd[KernelFsPathMax];
	strcpy(tempPwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(tempPwd, &dirname, &basename);
	assert(dirname==tempPwd);
	procData.pwd=ProcManEnvVarsVirtualOffset+envVarDataLen;
	strcpy(envVarData+envVarDataLen, dirname);
	envVarDataLen+=strlen(dirname)+1;

	strcpy(tempPath, "/usr/games:/usr/bin:/bin:");
	procData.path=ProcManEnvVarsVirtualOffset+envVarDataLen;
	strcpy(envVarData+envVarDataLen, tempPath);
	envVarDataLen+=strlen(tempPath)+1;

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
	procManData.processes[pid].procFd=kernelFsFileOpen(procPath, KernelFsFdModeRW);
	if (procManData.processes[pid].procFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not open process data file at '%s'\n"), procPath);
		goto error;
	}

	ramFd=kernelFsFileOpen(ramPath, KernelFsFdModeRW);
	if (ramFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not create new process - could not open ram data file at '%s'\n"), ramPath);
		goto error;
	}

	// Initialise state
	procManData.processes[pid].state=ProcManProcessStateActive;
	procManData.processes[pid].instructionCounter=0;
#ifndef ARDUINO
	memset(procManData.processes[pid].profilingCounts, 0, sizeof(procManData.processes[pid].profilingCounts[0])*BytecodeMemoryProgmemSize);
#endif

	// Initialise proc file (and env var data in ram file)
	procData.regs[BytecodeRegisterIP]=0;
	for(BytecodeSignalId i=0; i<BytecodeSignalIdNB; ++i)
		procData.signalHandlers[i]=ProcManSignalHandlerInvalid;
	for(ProcManLocalFd localFd=1; localFd<ProcManMaxFds; ++localFd)
		procData.fds[localFd-1]=ProcManLocalFdInvalid;
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

void procManProcessKill(ProcManPid pid, ProcManExitStatus exitStatus, ProcManProcessProcData *procDataGiven) {
	assert(pid<ProcManPidMax);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("attempting to kill process %u with exit status %u\n"), pid, exitStatus);

	// Not even open?
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL) {
		kernelLog(LogTypeWarning, kstrP("could not kill process %u - no such process\n"), pid);
		return;
	}

#ifndef ARDUINO
	// Save profiling counts to file before we start clearing things
	if (kernelFlagProfile) {
		char profilingFilePath[1024]; // TODO: this better
		char profilingExecBaseNameRaw[1024]="unknown"; // TODO: better
		char *profilingExecBaseName=profilingExecBaseNameRaw;
		if (process->progmemFd!=KernelFsFdInvalid) {
			kstrStrcpy(profilingExecBaseNameRaw, kernelFsGetFilePath(process->progmemFd));
			profilingExecBaseName=basename(profilingExecBaseNameRaw);
		}
		sprintf(profilingFilePath, "profile.%"PRIu64".%s.%u", ktimeGetMonotonicMs(), profilingExecBaseName, pid);
		FILE *profilingFile=fopen(profilingFilePath, "w");
		if (profilingFile!=NULL) {
			// Determine highest address instruction that was executed
			// (this usually greatly reduces output file size)
			BytecodeWord profilingFinal=0;
			for(BytecodeWord i=0; i<BytecodeMemoryProgmemSize; ++i)
				if (procManData.processes[pid].profilingCounts[i]>0)
					profilingFinal=i;

			// Write data and close file
			fwrite(procManData.processes[pid].profilingCounts, sizeof(ProfileCounter), profilingFinal+1, profilingFile);
			fclose(profilingFile);
		}
	}
#endif

	// Attempt to get/load proc data, but note this is not critical (after all, we may be here precisely because we could not read the proc data file).
	ProcManProcessProcData *procData=NULL;
	ProcManProcessProcData procDataRaw;
	if (procDataGiven!=NULL)
		procData=procDataGiven;
	else if (procManProcessLoadProcData(process, &procDataRaw))
		procData=&procDataRaw;

	// Close files left open by process
	if (procData!=NULL) {
		for(ProcManLocalFd localFd=1; localFd<ProcManMaxFds; ++localFd)
			if (procData->fds[localFd-1]!=KernelFsFdInvalid)
				procManProcessCloseFile(process, procData, localFd);
	}

	// Close proc and ram files, deleting tmp ones
	kernelFsFileClose(process->progmemFd);
	process->progmemFd=KernelFsFdInvalid;

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
#ifndef ARDUINO
	memset(process->profilingCounts, 0, sizeof(process->profilingCounts[0])*BytecodeMemoryProgmemSize);
#endif

	// Write to log
	if (procData!=NULL)
		kernelLog(LogTypeInfo, kstrP("killed process %u (post-IP=%u=0x%04X)\n"), pid, procData->regs[BytecodeRegisterIP], procData->regs[BytecodeRegisterIP]);
	else
		kernelLog(LogTypeInfo, kstrP("killed process %u\n"), pid);

	// Check if any processes are waiting due to waitpid syscall
	for(ProcManPid waiterPid=0; waiterPid<ProcManPidMax; ++waiterPid) {
		ProcManProcess *waiterProcess=procManGetProcessByPid(waiterPid);
		if (waiterProcess!=NULL && waiterProcess->state==ProcManProcessStateWaitingWaitpid && waiterProcess->stateData.waitingWaitpid.pid==pid) {
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
	assert(pid<ProcManPidMax);

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
			if (process->stateData.waitingWaitpid.timeoutTime>0 && ktimeGetMonotonicMs()>=process->stateData.waitingWaitpid.timeoutTime) {
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
			if (kernelFsFileCanRead(process->stateData.waitingRead.globalFd)) {
				// It is - load process data so we can update the state and read the data
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (read available) - could not load proc data, killing\n"), pid);
					goto kill;
				}

				process->state=ProcManProcessStateActive;
				if (!procManProcessRead(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (read available) - failed during read, killing\n"), pid);
					goto kill;
				}
			} else {
				// Otherwise process stays waiting
				return;
			}
		} break;
		case ProcManProcessStateWaitingRead32: {
			// Is data now available?
			if (kernelFsFileCanRead(process->stateData.waitingRead32.globalFd)) {
				// It is - load process data so we can update the state and read the data
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (read32 available) - could not load proc data, killing\n"), pid);
					goto kill;
				}

				process->state=ProcManProcessStateActive;
				if (!procManProcessRead32(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (read32 available) - failed during read, killing\n"), pid);
					goto kill;
				}
			} else {
				// Otherwise process stays waiting
				return;
			}
		} break;
		case ProcManProcessStateWaitingWrite: {
			// Is data now available?
			if (kernelFsFileCanWrite(process->stateData.waitingWrite.globalFd)) {
				// It is - load process data so we can update the state and write the data
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (write available) - could not load proc data, killing\n"), pid);
					goto kill;
				}

				process->state=ProcManProcessStateActive;
				if (!procManProcessWrite(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (write available) - failed during write, killing\n"), pid);
					goto kill;
				}
			} else {
				// Otherwise process stays waiting
				return;
			}
		} break;
		case ProcManProcessStateWaitingWrite32: {
			// Is data now available?
			if (kernelFsFileCanWrite(process->stateData.waitingWrite32.globalFd)) {
				// It is - load process data so we can update the state and write the data
				if (!procManProcessLoadProcData(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (write32 available) - could not load proc data, killing\n"), pid);
					goto kill;
				}

				process->state=ProcManProcessStateActive;
				if (!procManProcessWrite32(process, &procData)) {
					kernelLog(LogTypeWarning, kstrP("process %u tick (write32 available) - failed during write, killing\n"), pid);
					goto kill;
				}
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
#ifndef ARDUINO
		// Update profiling info (before we update IP register)
		if (procData.regs[BytecodeRegisterIP]<BytecodeMemoryProgmemSize)
			++procManData.processes[pid].profilingCounts[procData.regs[BytecodeRegisterIP]];
#endif

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
	// Check pid
	if (pid>ProcManPidMax) {
		kernelLog(LogTypeWarning, kstrP("could not send signal %u to process %u, bad pid\n"), signalId, pid);
		return;
	}

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
		kernelLog(LogTypeWarning, kstrP("could not send signal %u to process %u, could not load process data\n"), signalId, pid);
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
		case ProcManProcessStateWaitingRead32:
			// Set process active again but set r0 to indicate read failed
			process->state=ProcManProcessStateActive;
			procData.regs[0]=0;
		break;
		case ProcManProcessStateWaitingWrite:
		case ProcManProcessStateWaitingWrite32:
			// Set process active again but set r0 to indicate write failed
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

bool procManProcessGetOpenGlobalFds(ProcManPid pid, KernelFsFd fds[ProcManMaxFds]) {
	// Grab process (if exists)
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return false;

	// Load proc data
	ProcManProcessProcData procData;
	if (!procManProcessLoadProcData(process, &procData))
		return false;

	// Copy fds table
	// Note that localFd=0 is the invalid fd, so need to shift up by one when copying
	fds[0]=KernelFsFdInvalid;
	memcpy(fds+1, procData.fds, sizeof(procData.fds));

	return true;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid) {
	if (pid>=ProcManPidMax)
		return NULL;
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid)
		return NULL;
	return procManData.processes+pid;
}

ProcManPid procManGetPidFromProcess(const ProcManProcess *process) {
	assert(process!=NULL);

	if (process<procManData.processes)
		return ProcManPidMax;

	uintptr_t pid=process-procManData.processes;
	if (pid>=ProcManPidMax)
		return ProcManPidMax;

	return pid;
}

const char *procManGetExecPathFromProcess(const ProcManProcess *process) {
	assert(process!=NULL);

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
	assert(process!=NULL);
	assert(procData!=NULL);

	return (kernelFsFileReadOffset(process->procFd, 0, (uint8_t *)procData, sizeof(ProcManProcessProcData))==sizeof(ProcManProcessProcData));
}

bool procManProcessStoreProcData(ProcManProcess *process, ProcManProcessProcData *procData) {
	assert(process!=NULL);
	assert(procData!=NULL);

	return (kernelFsFileWriteOffset(process->procFd, 0, (const uint8_t *)procData, sizeof(ProcManProcessProcData))==sizeof(ProcManProcessProcData));
}

bool procManProcessLoadProcDataRamLen(const ProcManProcess *process, uint16_t *value) {
	assert(process!=NULL);
	assert(value!=NULL);

	return (process->procFd!=KernelFsFdInvalid && kernelFsFileReadOffset(process->procFd, offsetof(ProcManProcessProcData,ramLen), (uint8_t *)value, sizeof(uint16_t))==sizeof(uint16_t));
}

bool procManProcessLoadProcDataEnvVarDataLen(const ProcManProcess *process, uint8_t *value) {
	assert(process!=NULL);
	assert(value!=NULL);

	return (process->procFd!=KernelFsFdInvalid && kernelFsFileReadOffset(process->procFd, offsetof(ProcManProcessProcData,envVarDataLen), value, sizeof(uint8_t))==sizeof(uint8_t));
}

bool procManProcessLoadProcDataRamFd(const ProcManProcess *process, KernelFsFd *ramFd) {
	assert(process!=NULL);
	assert(ramFd!=NULL);

	return (process->procFd!=KernelFsFdInvalid && kernelFsFileReadOffset(process->procFd, offsetof(ProcManProcessProcData,ramFd), (uint8_t *)ramFd, sizeof(KernelFsFd))==sizeof(KernelFsFd));
}

bool procManProcessSaveProcDataReg(const ProcManProcess *process, BytecodeRegister reg, BytecodeWord value) {
	assert(process!=NULL);

	return (process->procFd!=KernelFsFdInvalid && kernelFsFileWriteOffset(process->procFd, offsetof(ProcManProcessProcData,regs)+sizeof(BytecodeWord)*reg, (uint8_t *)&value, sizeof(BytecodeWord))==sizeof(BytecodeWord));
}

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t *value) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(value!=NULL);

	return procManProcessMemoryReadBlock(process, procData, addr, value, 1, true);
}

bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord *value) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(value!=NULL);

	uint8_t parts[2];
	if (!procManProcessMemoryReadBlock(process, procData, addr, parts, 2, true))
		return false;

	*value=(((BytecodeWord)parts[0])<<8)|((BytecodeWord)parts[1]);

	return true;
}

bool procManProcessMemoryReadDoubleWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeDoubleWord *value) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(value!=NULL);

	uint8_t parts[4];
	if (!procManProcessMemoryReadBlock(process, procData, addr, parts, 4, true))
		return false;

	*value=(((BytecodeDoubleWord)parts[0])<<24)|(((BytecodeDoubleWord)parts[1])<<16)|(((BytecodeDoubleWord)parts[2])<<8)|((BytecodeDoubleWord)parts[3]);

	return true;
}

bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, char *str, uint16_t len) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(str!=NULL);

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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(data!=NULL);

	if (addr+len<BytecodeMemoryRamAddr) {
		// Addresss is in progmem data
		if (kernelFsFileReadOffset(process->progmemFd, addr, data, len)==len)
			return true;
		else {
			if (verbose)
				kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read invalid address (0x%04X, pointing to PROGMEM at offset %u, len %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, addr, len);
			return false;
		}
	} else if (addr>=BytecodeMemoryRamAddr) {
		// Address is in RAM
		BytecodeWord ramIndex=(addr-BytecodeMemoryRamAddr);
		if (ramIndex<procData->ramLen) {
			// Standard RAM
			if (kernelFsFileReadOffset(procData->ramFd, ramIndex+procData->envVarDataLen, data, len)!=len) {
				if (verbose)
					kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read valid address (0x%04X, len %u) but failed, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len);
				return false;
			}
			return true;
		} else if (addr>=ProcManEnvVarsVirtualOffset) {
			// Specially mapped top 1kb of RAM (which is actually located at the start of the ram file)
			BytecodeWord offset=(addr-ProcManEnvVarsVirtualOffset);
			if (offset<procData->envVarDataLen) {
				if (kernelFsFileReadOffset(procData->ramFd, offset, data, len)!=len) {
					if (verbose)
						kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read valid address (0x%04X, len %u) but failed, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len);
					return false;
				}
				return true;
			} else {
				if (verbose)
					kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read invalid address (0x%04X, len %u, but EnvVarData size is only %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len, procData->envVarDataLen);
				return false;
			}
		} else {
			if (verbose)
				kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to read invalid address (0x%04X, len %u, but RAM size is only %u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len, procData->ramLen);
			return false;
		}
	} else
		// Block spans both regions of memory, invalid call.
		return false;
}

bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, uint8_t value) {
	assert(process!=NULL);
	assert(procData!=NULL);

	return procManProcessMemoryWriteBlock(process, procData, addr, &value, 1);
}

bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeWord value) {
	assert(process!=NULL);
	assert(procData!=NULL);

	uint8_t data[2]={value>>8, value&255};
	return procManProcessMemoryWriteBlock(process, procData, addr, data, 2);
}

bool procManProcessMemoryWriteDoubleWord(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, BytecodeDoubleWord value) {
	assert(process!=NULL);
	assert(procData!=NULL);

	uint8_t data[4]={(value>>24)&255, (value>>16)&255, (value>>8)&255, value&255};
	return procManProcessMemoryWriteBlock(process, procData, addr, data, 4);
}

bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const char *str) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(str!=NULL);

	return procManProcessMemoryWriteBlock(process, procData, addr, (const uint8_t *)str, strlen(str)+1);
}

bool procManProcessMemoryWriteBlock(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeWord addr, const uint8_t *data, uint16_t len) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(data!=NULL);

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
		// Standard RAM
		if (kernelFsFileWriteOffset(procData->ramFd, procData->envVarDataLen+ramIndex, data, len)!=len) {
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to valid RAM address (0x%04X, len %u), but failed, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len);
			return false;
		}
		return true;
	} else if (addr>=ProcManEnvVarsVirtualOffset) {
		// Special upper 1kb of RAM (mapped from start of file)
		BytecodeWord offset=addr-ProcManEnvVarsVirtualOffset;
		if (kernelFsFileWriteOffset(procData->ramFd, offset, data, len)!=len) {
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to valid RAM address (0x%04X, len %u), but failed, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, len);
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
		procData->ramFd=kernelFsFileOpen(ramFdPath, KernelFsFdModeRW);
		if (procData->ramFd==KernelFsFdInvalid) {
			kernelLog(LogTypeWarning, kstrP("process %u (%s) tried to write to RAM (0x%04X, offset %u, len %u), beyond size, but could not reopen file after resizing, killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, len);
			kernelFsFileDelete(ramFdPath);
			goto error;
		}

		// Update stored ram len and write data
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

bool procManProcessGetArgvNAddr(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, BytecodeWord *addr) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(addr!=NULL);

	// Check n is sensible
	if (n>=ARGVMAX || n>=procData->argc) {
		*addr=0;
		return true;
	}

	// Loop over args until we find the start of the one asked for
	*addr=ProcManEnvVarsVirtualOffset;
	while(n>0) {
		// Read a character
		uint8_t c;
		if (!procManProcessMemoryReadByte(process, procData, (*addr)++, &c)) {
			kernelLog(LogTypeWarning, kstrP("corrupt argvdata or ram file more generally? Process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			return false;
		}

		// If hit terminator then next character will be start of a new argument
		if (c=='\0')
			--n;
	}

	return true;
}

bool procManProcessGetArgvNStr(ProcManProcess *process, ProcManProcessProcData *procData, uint8_t n, char *str) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(str!=NULL);

	// Check n is sensible
	if (n>=ARGVMAX || n>=procData->argc) {
		strcpy(str, "");
		return true;
	}

	// Loop over args until we find the start of the one asked for
	BytecodeWord addr=ProcManEnvVarsVirtualOffset;
	while(n>0) {
		// Read a character
		uint8_t c;
		if (!procManProcessMemoryReadByte(process, procData, addr++, &c)) {
			kernelLog(LogTypeWarning, kstrP("corrupt argvdata or ram file more generally? Process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			return false;
		}

		// If hit terminator then next character will be start of a new argument
		if (c=='\0')
			--n;
	}

	// Copy found argv into given str pointer
	if (!procManProcessMemoryReadStr(process, procData, addr, str, UINT16_MAX)) {
		kernelLog(LogTypeWarning, kstrP("corrupt argvdata or ram file more generally? Process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return false;
	}

	return true;
}

bool procManProcessGetInstruction(ProcManProcess *process, ProcManProcessProcData *procData, ProcManPrefetchData *prefetchData, BytecodeInstruction3Byte *instruction) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(prefetchData!=NULL);
	assert(instruction!=NULL);

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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(prefetchData!=NULL);
	assert(exitStatus!=NULL);

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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(info!=NULL);
	assert(exitStatus!=NULL);

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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(info!=NULL);
	assert(prefetchData!=NULL);
	assert(exitStatus!=NULL);

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
			procData->regs[info->d.alu.destReg]=(opB<16 ? opA<<opB : 0);
			return true;
		break;
		case BytecodeInstructionAluTypeShiftRight:
			procData->regs[info->d.alu.destReg]=(opB<16 ? opA>>opB : 0);
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
				case BytecodeInstructionAluExtraTypeClz: {
					BytecodeWord srcValue=procData->regs[info->d.alu.opAReg];
					procData->regs[info->d.alu.destReg]=clz16(srcValue);
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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(info!=NULL);
	assert(prefetchData!=NULL);
	assert(exitStatus!=NULL);

	switch(info->d.misc.type) {
		case BytecodeInstructionMiscTypeNop:
			return true;
		break;
		case BytecodeInstructionMiscTypeSyscall:
			return procManProcessExecSyscall(process, procData, exitStatus);
		break;
		case BytecodeInstructionMiscTypeIllegal:
			kernelLog(LogTypeWarning, kstrP("illegal instruction, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
			return false;
		break;
		case BytecodeInstructionMiscTypeClearInstructionCache:
			procManPrefetchDataClear(prefetchData);
			return true;
		break;
		case BytecodeInstructionMiscTypeDebug:
			procManProcessDebug(process, procData);
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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(exitStatus!=NULL);

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

			if (!procManProcessGetArgvNAddr(process, procData, n, &procData->regs[0])) {
				kernelLog(LogTypeWarning, kstrP("failed during exec getargvn, n=%u, process %u (%s), killing\n"), n, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
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
			if (waitPid>=ProcManPidMax || procManGetProcessByPid(waitPid)==NULL) {
				procData->regs[0]=ProcManExitStatusNoProcess;
			} else {
				// Otherwise indicate process is waiting for this pid to die
				process->state=ProcManProcessStateWaitingWaitpid;
				process->stateData.waitingWaitpid.pid=waitPid;
				process->stateData.waitingWaitpid.timeoutTime=(timeout>0 ? ktimeGetMonotonicMs()+timeout*1000llu : 0);
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
					case ProcManProcessStateWaitingRead32:
					case ProcManProcessStateWaitingWrite:
					case ProcManProcessStateWaitingWrite32:
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

			// Special case for suicide signal sent to init
			if (targetPid==0 && signalId==BytecodeSignalIdSuicide) {
				kernelLog(LogTypeWarning, kstrP("process %u - warning cannot send signal 'suicide' to init (target pid=%u)\n"), procManGetPidFromProcess(process), targetPid);
				return true;
			}

			// Otherwise standard target and signal
			procManProcessSendSignal(targetPid, signalId);

			return true;
		} break;
		case BytecodeSyscallIdGetPidFdN: {
			// Grab arguments
			ProcManPid pid=procData->regs[1];
			ProcManLocalFd localFd=procData->regs[2];

			// Bad pid or localFd?
			if (pid>=ProcManPidMax || localFd==ProcManLocalFdInvalid || localFd>=ProcManMaxFds) {
				procData->regs[0]=KernelFsFdInvalid;
				return true;
			}

			// If pid represents program making the syscall then all data is already loaded
			// Otherwise we have to load proc data for given pid
			if (pid==procManGetPidFromProcess(process))
				procData->regs[0]=procData->fds[localFd-1];
			else
				procData->regs[0]=procManProcessGetGlobalFdFromLocalWithPid(pid, localFd);

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
			ProcManLocalFd localFd=procData->regs[1];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during read syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Check if would block
			if (!kernelFsFileCanRead(globalFd)) {
				// Reading would block - so enter waiting state until data becomes available.
				process->state=ProcManProcessStateWaitingRead;
				process->stateData.waitingRead.globalFd=globalFd;
				return true;
			}

			// Otherwise read as normal (stopping before we would block)
			if (!procManProcessRead(process, procData)) {
				kernelLog(LogTypeWarning, kstrP("failed during read syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdWrite: {
			ProcManLocalFd localFd=procData->regs[1];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during write syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Check if would block
			if (!kernelFsFileCanWrite(globalFd)) {
				// Writing would block - so enter waiting state until space becomes available.
				process->state=ProcManProcessStateWaitingWrite;
				process->stateData.waitingWrite.globalFd=globalFd;
				return true;
			}

			// Otherwise write as normal (stopping before we would block)
			if (!procManProcessWrite(process, procData)) {
				kernelLog(LogTypeWarning, kstrP("failed during write syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdOpen: {
			BytecodeWord pathAddr=procData->regs[1];
			BytecodeWord mode=procData->regs[2]; // we use BytecodeWord type here instead of KernelFsFdMode in case value does not fit in 8 bits

			// Check if mode is valid
			if (mode==KernelFsFdModeNone || mode>=KernelFsFdModeMax) {
				procData->regs[0]=ProcManLocalFdInvalid;
				return true;
			}

			// Read path from user space
			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during open syscall, could not read path, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			// Attempt to open file (adding to fd table etc)
			procData->regs[0]=procManProcessOpenFile(process, procData, path, mode);

			return true;
		} break;
		case BytecodeSyscallIdClose: {
			ProcManLocalFd localFd=procData->regs[1];

			// Close file (and remove from fd table etc)
			procManProcessCloseFile(process, procData, localFd);

			return true;
		} break;
		case BytecodeSyscallIdDirGetChildN: {
			ProcManLocalFd localFd=procData->regs[1];
			BytecodeWord childNum=procData->regs[2];
			uint16_t bufAddr=procData->regs[3];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during dirgetchildn syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Get nth child
			char childPath[KernelFsPathMax];
			procData->regs[0]=kernelFsDirectoryGetChild(globalFd, childNum, childPath);
			if (procData->regs[0] && !procManProcessMemoryWriteStr(process, procData, bufAddr, childPath)) {
				kernelLog(LogTypeWarning, kstrP("failed during dirgetchildn syscall, local fd %u, global fd %u process %u (%s), killing\n"), localFd, globalFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdGetPath: {
			ProcManLocalFd localFd=procData->regs[1];
			uint16_t bufAddr=procData->regs[2];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during getpath syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Grab file path
			assert(!kstrIsNull(kernelFsGetFilePath(globalFd)));
			kstrStrcpy(procManScratchBufPath2, kernelFsGetFilePath(globalFd));

			// Write path into process memory
			if (!procManProcessMemoryWriteStr(process, procData, bufAddr, procManScratchBufPath2)) {
				kernelLog(LogTypeWarning, kstrP("failed during getpath syscall, localFd %u, globalFd %u, process %u (%s), killing\n"), localFd, globalFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			procData->regs[0]=1;

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
		case BytecodeSyscallIdGetFileLen: {
			uint16_t pathAddr=procData->regs[1];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during getfilelen syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			if (kernelFsPathIsValid(path)) {
				KernelFsFileOffset fileLen=kernelFsFileGetLen(path);
				if (fileLen>65535)
					fileLen=65535;
				procData->regs[0]=fileLen;
			} else
				procData->regs[0]=0;

			return true;
		} break;
		case BytecodeSyscallIdTryReadByte: {
			ProcManLocalFd localFd=procData->regs[1];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during tryreadbyte syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=256;
				return true;
			}

			// Save terminal settings and change to avoid waiting for newline
			// We do this incase the given fd ends up depending on /dev/ttyS0, either directly or indirectly.
			bool prevBlocking=ttyGetBlocking();
			ttySetBlocking(false);

			// Attempt to read
			uint8_t value;
			KernelFsFileOffset readResult=kernelFsFileReadOffset(globalFd, 0, &value, 1);

			// Restore terminal settings
			ttySetBlocking(prevBlocking);

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
		case BytecodeSyscallIdRead32: {
			ProcManLocalFd localFd=procData->regs[1];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during read32 syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Check if would block
			if (!kernelFsFileCanRead(globalFd)) {
				// Reading would block - so enter waiting state until data becomes available.
				process->state=ProcManProcessStateWaitingRead32;
				process->stateData.waitingRead32.globalFd=globalFd;
				return true;
			}

			// Otherwise read as normal (stopping before we would block)
			if (!procManProcessRead32(process, procData)) {
				kernelLog(LogTypeWarning, kstrP("failed during read32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdWrite32: {
			ProcManLocalFd localFd=procData->regs[1];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during write32 syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Check if would block
			if (!kernelFsFileCanWrite(globalFd)) {
				// Writing would block - so enter waiting state until space becomes available.
				process->state=ProcManProcessStateWaitingWrite32;
				process->stateData.waitingWrite32.globalFd=globalFd;
				return true;
			}

			// Otherwise write as normal (stopping before we would block)
			if (!procManProcessWrite32(process, procData)) {
				kernelLog(LogTypeWarning, kstrP("failed during write32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdResizeFile32: {
			// Grab path and new size
			uint16_t pathAddr=procData->regs[1];
			uint16_t newSizePtr=procData->regs[2];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
					kernelLog(LogTypeWarning, kstrP("failed during resizefile32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			KernelFsFileOffset newSize;
			if (!procManProcessMemoryReadDoubleWord(process, procData, newSizePtr, &newSize)) {
					kernelLog(LogTypeWarning, kstrP("failed during resizefile32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Resize (or create if does not exist)
			if (!kernelFsPathIsValid(path))
				procData->regs[0]=0;
			else if (kernelFsFileExists(path))
				procData->regs[0]=kernelFsFileResize(path, newSize);
			else
				procData->regs[0]=kernelFsFileCreateWithSize(path, newSize);

			return true;
		} break;
		case BytecodeSyscallIdGetFileLen32: {
			uint16_t pathAddr=procData->regs[1];
			uint16_t destPtr=procData->regs[2];

			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during getfilelen32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			BytecodeDoubleWord size=(kernelFsPathIsValid(path) ? kernelFsFileGetLen(path) : 0);
			if (!procManProcessMemoryWriteDoubleWord(process, procData, destPtr, size)) {
				kernelLog(LogTypeWarning, kstrP("failed during getfilelen32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case BytecodeSyscallIdAppend: {
			uint16_t pathAddr=procData->regs[1];
			uint16_t dataAddr=procData->regs[2];
			uint16_t dataLen=procData->regs[3];

			// Grab path
			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during append syscall, could not read path, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			if (!kernelFsPathIsValid(path)) {
				procData->regs[0]=0;
				return true;
			}

			// Grab file's current length
			KernelFsFileOffset oldFileLen=kernelFsFileGetLen(path);

			// If zero length attempt to create
			KernelFsFileOffset newFileLen=oldFileLen+dataLen;
			if (oldFileLen==0) {
				// Note we do not check return as may already exist but just with 0 length, so this will fail in that case yet we still want to continue.
				kernelFsFileCreateWithSize(path, newFileLen);
			}

			// Attempt to resize file (unless character device which has no size but can consume any number of bytes)
			if (!kernelFsFileIsCharacter(path) && !kernelFsFileResize(path, newFileLen)) {
				procData->regs[0]=0;
				return true;
			}

			// Attempt to open file
			KernelFsFd fd=kernelFsFileOpen(path, KernelFsFdModeWO);
			if (fd==KernelFsFdInvalid) {
				procData->regs[0]=0;
				return true;
			}

			// Write data to file
			uint16_t i=0;
			while(i<dataLen) {
				KernelFsFileOffset chunkSize=dataLen-i;
				if (chunkSize>256)
					chunkSize=256;

				if (!procManProcessMemoryReadBlock(process, procData, dataAddr+i, (uint8_t *)procManScratchBuf256, chunkSize, true)) {
					kernelFsFileClose(fd);
					kernelLog(LogTypeWarning, kstrP("failed during append syscall, could not read data chunk at %u size %u, process %u (%s), killing\n"), dataAddr+i, chunkSize, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				if (kernelFsFileWriteOffset(fd, oldFileLen+i, (const uint8_t *)procManScratchBuf256, chunkSize)!=chunkSize) {
					kernelFsFileClose(fd);
					procData->regs[0]=0;
					return true;
				}

				i+=chunkSize;
			}

			// Close file
			kernelFsFileClose(fd);

			// Indicate success
			procData->regs[0]=1;

			// Write to log
			kernelLog(LogTypeInfo, kstrP("append syscall: path='%s', dataLen=%u, process %u (%s)\n"), path, dataLen, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));

			return true;
		} break;
		case BytecodeSyscallIdFlush: {
			uint16_t pathAddr=procData->regs[1];

			// Grab path
			char path[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during flush syscall, could not read path, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(path);

			if (!kernelFsPathIsValid(path)) {
				procData->regs[0]=0;
				return true;
			}

			// Flush file/directory
			procData->regs[0]=kernelFsFileFlush(path);

			return true;
		} break;
		case BytecodeSyscallIdTryWriteByte: {
			ProcManLocalFd localFd=procData->regs[1];
			uint8_t value=procData->regs[2];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during trywritebyte syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				procData->regs[0]=0;
				return true;
			}

			// Attempt to write
			procData->regs[0]=kernelFsFileWriteOffset(globalFd, 0, &value, 1);

			return true;
		} break;
		case BytecodeSyscallIdGetPathGlobal: {
			KernelFsFd globalFd=procData->regs[1];
			uint16_t bufAddr=procData->regs[2];

			// Grab file path
			if (kstrIsNull(kernelFsGetFilePath(globalFd))) {
				procData->regs[0]=0;
				return true;
			}
			kstrStrcpy(procManScratchBufPath2, kernelFsGetFilePath(globalFd));

			// Write path into process memory
			if (!procManProcessMemoryWriteStr(process, procData, bufAddr, procManScratchBufPath2)) {
				kernelLog(LogTypeWarning, kstrP("failed during getpathglobal syscall, globalFd %u, process %u (%s), killing\n"), globalFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			procData->regs[0]=1;

			return true;
		} break;
		case BytecodeSyscallIdEnvGetPwd: {
			procData->regs[0]=procData->pwd;
			return true;
		} break;
		case BytecodeSyscallIdEnvSetPwd: {
			BytecodeWord addr=procData->regs[1];

			procData->pwd=addr;

			return true;
		} break;
		case BytecodeSyscallIdEnvGetPath: {
			procData->regs[0]=procData->path;
			return true;
		} break;
		case BytecodeSyscallIdEnvSetPath: {
			BytecodeWord addr=procData->regs[1];

			procData->path=addr;

			return true;
		} break;
		case BytecodeSyscallIdTimeMonotonic16s:
			procData->regs[0]=(ktimeGetMonotonicMs()/1000);
			return true;
		break;
		case BytecodeSyscallIdTimeMonotonic16ms: {
			procData->regs[0]=ktimeGetMonotonicMs();
			return true;
		} break;
		case BytecodeSyscallIdTimeMonotonic32s: {
			BytecodeWord destPtr=procData->regs[1];

			BytecodeDoubleWord result=(ktimeGetMonotonicMs()/1000);
			if (!procManProcessMemoryWriteDoubleWord(process, procData, destPtr, result)) {
				kernelLog(LogTypeWarning, kstrP("failed during timemonotonic32s syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			return true;
		} break;
		case BytecodeSyscallIdTimeMonotonic32ms: {
			BytecodeWord destPtr=procData->regs[1];

			BytecodeDoubleWord result=ktimeGetMonotonicMs();
			if (!procManProcessMemoryWriteDoubleWord(process, procData, destPtr, result)) {
				kernelLog(LogTypeWarning, kstrP("failed during timemonotonic32ms syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			return true;
		} break;
		case BytecodeSyscallIdTimeReal32s: {
			BytecodeWord destPtr=procData->regs[1];

			BytecodeDoubleWord result=ktimeGetRealMs()/1000;
			if (!procManProcessMemoryWriteDoubleWord(process, procData, destPtr, result)) {
				kernelLog(LogTypeWarning, kstrP("failed during timereal32s syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			return true;
		} break;
		case BytecodeSyscallIdTimeToDate32s: {
			// Grab arguments
			BytecodeWord destDatePtr=procData->regs[1];
			BytecodeWord srcTimePtr=procData->regs[2];

			BytecodeDoubleWord srcTime;
			if (!procManProcessMemoryReadDoubleWord(process, procData, srcTimePtr, &srcTime)) {
				kernelLog(LogTypeWarning, kstrP("failed during timetodate32s syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Decompose into date
			KDate date;
			ktimeTimeMsToDate(srcTime*((KTime)1000), &date);

			// User space date struct has a slightly different structure.
			// So have to copy field by field.
			if (!procManProcessMemoryWriteWord(process, procData, destDatePtr+0, date.year) ||
			    !procManProcessMemoryWriteByte(process, procData, destDatePtr+2, date.month) ||
			    !procManProcessMemoryWriteByte(process, procData, destDatePtr+3, date.day) ||
			    !procManProcessMemoryWriteByte(process, procData, destDatePtr+4, date.hour) ||
			    !procManProcessMemoryWriteByte(process, procData, destDatePtr+5, date.minute) ||
			    !procManProcessMemoryWriteByte(process, procData, destDatePtr+6, date.second)) {
				kernelLog(LogTypeWarning, kstrP("failed during timetodate32s syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
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
			uint16_t dirPathAddr=procData->regs[1];

			char dirPath[KernelFsPathMax];
			if (!procManProcessMemoryReadStr(process, procData, dirPathAddr, dirPath, KernelFsPathMax)) {
				kernelLog(LogTypeWarning, kstrP("failed during mount syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}
			kernelFsPathNormalise(dirPath);

			// Unmount
			kernelUnmount(dirPath);

			return true;
		} break;
		case BytecodeSyscallIdIoctl: {
			// Grab arguments
			ProcManLocalFd localFd=procData->regs[1];
			uint16_t command=procData->regs[2];
			uint16_t data=procData->regs[3];

			// Get global fd
			KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
			if (globalFd==KernelFsFdInvalid) {
				kernelLog(LogTypeWarning, kstrP("failed during ioctl syscall, local fd %u not open, process %u (%s)\n"), localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return true;
			}

			// Grab path
			KStr kstrPath=kernelFsGetFilePath(globalFd);
			assert(!kstrIsNull(kstrPath));
			kstrStrcpy(procManScratchBufPath0, kstrPath);

			// Check for stdin/stdout terminal
			if (strcmp(procManScratchBufPath0, "/dev/ttyS0")==0) {
				switch(command) {
					case BytecodeSyscallIdIoctlCommandDevTtyS0SetEcho:
						ttySetEcho((data!=0));
					break;
					default:
						kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall command %u (local fd %u, global fd %u, device '%s'), process %u (%s)\n"), command, localFd, globalFd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					break;
				}
			} else if (strncmp(procManScratchBufPath0, "/dev/pin", 8)==0) {
				// Check for pin device file path
				uint8_t pinNum=atoi(procManScratchBufPath0+8); // TODO: Verify valid number (e.g. currently '/dev/pin' will operate pin0 (although the file /dev/pin must exist so should be fine for now)
				switch(command) {
					case BytecodeSyscallIdIoctlCommandDevPinSetMode: {
						// Forbid mode changes to pins which are in use (for things such as HW devices or the SPI bus).
						if (pinInUse(pinNum)) {
							kernelLog(LogTypeWarning, kstrP("ioctl attempting to set mode of pin %u but it is in use (local fd %u, global fd %u, device '%s'), process %u (%s)\n"), pinNum, localFd, globalFd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
							break;
						}

						// Set pin mode
						pinSetMode(pinNum, (data==PinModeInput ? PinModeInput : PinModeOutput));
					} break;
					default:
						kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall command %u (local fd %u, global fd %u, device '%s'), process %u (%s)\n"), command, localFd, globalFd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					break;
				}
			} else {
				kernelLog(LogTypeWarning, kstrP("invalid ioctl syscall device (local fd %u, global fd %u, device '%s'), process %u (%s)\n"), localFd, globalFd, procManScratchBufPath0, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
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
		case BytecodeSyscallIdPipeOpen: {
			// Grab arguments
			BytecodeWord readFdPtr=procData->regs[1];
			BytecodeWord writeFdPtr=procData->regs[2];

			// Determine paths to two files that will be used to create the pipe
			// We start pipeId at 1 so we can parse the path later using atoi with 0 as an error
			unsigned pipeId;
			for(pipeId=1; pipeId<ProcManMaxPipes; ++pipeId) {
				sprintf(procManScratchBufPath0, "/tmp/pipe%u", pipeId); // backing file path
				if (kernelFsFileExists(procManScratchBufPath0))
					continue;
				sprintf(procManScratchBufPath1, "/dev/pipe%u", pipeId); // circular buffer path
				if (kernelFsFileExists(procManScratchBufPath1))
					continue;
				break;
			}

			if (pipeId==ProcManMaxPipes) {
				procData->regs[0]=0;
				kernelLog(LogTypeWarning, kstrP("error during pipeopen syscall, process %u (%s) - global pipe limit %u reached\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), procData->regs[0], ProcManMaxPipes);
				return true;
			}

			// Create backing file
			if (!kernelFsFileCreateWithSize(procManScratchBufPath0, ProcManPipeSize)) {
				procData->regs[0]=0;
				kernelLog(LogTypeWarning, kstrP("error during pipeopen syscall, process %u (%s) - cannot create backing file (pipeId=%u)\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), procData->regs[0], pipeId);
				return true;
			}

			// Mount backing file as circular buffer to act as a pipe
			if (!kernelMount(KernelMountFormatCircBuf, procManScratchBufPath0, procManScratchBufPath1)) {
				kernelFsFileDelete(procManScratchBufPath0);
				procData->regs[0]=0;
				kernelLog(LogTypeWarning, kstrP("error during pipeopen syscall, process %u (%s) - could not mount circular buffer (pipeId=%u)\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), procData->regs[0], pipeId);
				return true;
			}

			// Open circular buffer file with two different fds - one for reading and one for writing
			// Note: we open like we would a file via the open syscall.
			// This is so that pipes can be reclaimed from a process once terminated.
			ProcManLocalFd readFd=procManProcessOpenFile(process, procData, procManScratchBufPath1, KernelFsFdModeRO);
			ProcManLocalFd writeFd=procManProcessOpenFile(process, procData, procManScratchBufPath1, KernelFsFdModeWO);
			if (readFd==ProcManLocalFdInvalid || writeFd==ProcManLocalFdInvalid) {
				procManProcessCloseFile(process, procData, readFd);
				procManProcessCloseFile(process, procData, writeFd);
				kernelUnmount(procManScratchBufPath1);
				kernelFsFileDelete(procManScratchBufPath0);

				procData->regs[0]=0;
				kernelLog(LogTypeWarning, kstrP("error during pipeopen syscall, process %u (%s) - could not open circular buffer (pipeId=%u)\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), procData->regs[0], pipeId);
				return true;
			}

			// Copy readFd and writeFd into userspace memory and the given pointers
			if (!procManProcessMemoryWriteByte(process, procData, readFdPtr, readFd) ||
			    !procManProcessMemoryWriteByte(process, procData, writeFdPtr, writeFd)) {
				procManProcessCloseFile(process, procData, readFd);
				procManProcessCloseFile(process, procData, writeFd);
				kernelUnmount(procManScratchBufPath1);
				kernelFsFileDelete(procManScratchBufPath0);

				procData->regs[0]=0;
				kernelLog(LogTypeWarning, kstrP("error during pipeopen syscall, process %u (%s) - could not copy readFd and writeFd to userspace (pipeId=%u), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), procData->regs[0], pipeId);
				return false;
			}

			// Write to log
			kernelLog(LogTypeInfo, kstrP("process %u (%s) openned pipe - local read fd %u, local write fd %u, pipeId %u\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), readFd, writeFd, pipeId);

			procData->regs[0]=1;

			return true;
		} break;
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
		case ByteCodeSyscallIdMemcmp: {
			uint16_t p1Addr=procData->regs[1];
			uint16_t p2Addr=procData->regs[2];
			uint16_t size=procData->regs[3];

			procData->regs[0]=0; // set here in case of size=0

			uint16_t i=0;
			while(i<size) {
				// Decide on chunk size
				// Note: we have to limit this due to using 256 byte buffer
				KernelFsFileOffset chunkSize=size-i;
				if (chunkSize>128)
					chunkSize=128;

				// Read from two pointers
				if (!procManProcessMemoryReadBlock(process, procData, p1Addr+i, ((uint8_t *)procManScratchBuf256)+0*chunkSize, chunkSize, true)) {
					kernelLog(LogTypeWarning, kstrP("failed during memcmp syscall reading, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				if (!procManProcessMemoryReadBlock(process, procData, p2Addr+i, ((uint8_t *)procManScratchBuf256)+1*chunkSize, chunkSize, true)) {
					kernelLog(LogTypeWarning, kstrP("failed during memcmp syscall reading, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}

				// Compare bytes
				procData->regs[0]=memcmp(((uint8_t *)procManScratchBuf256)+0*chunkSize, ((uint8_t *)procManScratchBuf256)+1*chunkSize, chunkSize);
				if (procData->regs[0]!=0)
					break;

				// Move onto next chunk
				i+=chunkSize;
			}

			return true;
		} break;
		case ByteCodeSyscallIdStrrchr: {
			uint16_t strAddr=procData->regs[1];
			uint16_t c=procData->regs[2];

			procData->regs[0]=0;
			while(1) {
				uint8_t value;
				if (!procManProcessMemoryReadByte(process, procData, strAddr, &value)) {
					kernelLog(LogTypeWarning, kstrP("failed during strrchr syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}

				if (value==c)
					procData->regs[0]=strAddr;
				if (value=='\0')
					break;

				strAddr++;
			}
			return true;
		} break;
		case ByteCodeSyscallIdStrcmp: {
			uint16_t p1Addr=procData->regs[1];
			uint16_t p2Addr=procData->regs[2];

			procData->regs[0]=0; // set here in case of empty strings

			uint16_t i=0;
			while(1) {
				// Decide on chunk size
				// Note: we have to limit this due to using 256 byte buffer
				KernelFsFileOffset chunkSize=128;

				// Read from two pointers
				if (!procManProcessMemoryReadStr(process, procData, p1Addr+i, ((char *)procManScratchBuf256)+0*chunkSize, chunkSize)) {
					kernelLog(LogTypeWarning, kstrP("failed during strcmp syscall reading, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}
				if (!procManProcessMemoryReadStr(process, procData, p2Addr+i, ((char *)procManScratchBuf256)+1*chunkSize, chunkSize)) {
					kernelLog(LogTypeWarning, kstrP("failed during strcmp syscall reading, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}

				// Compare bytes
				procData->regs[0]=strncmp(((char *)procManScratchBuf256)+0*chunkSize, ((char *)procManScratchBuf256)+1*chunkSize, chunkSize);
				if (procData->regs[0]!=0 || memchr(((char *)procManScratchBuf256)+0*chunkSize, '\0', chunkSize)!=NULL)
					break;

				// Move onto next chunk
				i+=chunkSize;
			}

			return true;
		} break;
		case ByteCodeSyscallIdMemchr: {
			uint16_t haystackAddr=procData->regs[1];
			uint16_t c=procData->regs[2];
			uint16_t size=procData->regs[3];

			procData->regs[0]=0;
			for(uint16_t i=0; i<size; ++i,++haystackAddr) {
				uint8_t value;
				if (!procManProcessMemoryReadByte(process, procData, haystackAddr, &value)) {
					kernelLog(LogTypeWarning, kstrP("failed during memchr syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
					return false;
				}

				if (value==c) {
					procData->regs[0]=haystackAddr;
					break;
				}
			}
			return true;
		} break;
		case BytecodeSyscallIdHwDeviceRegister: {
			// Grab arguments
			HwDeviceId id=procData->regs[1];
			HwDeviceType type=procData->regs[2];
			BytecodeWord pinsAddr=procData->regs[3];

			// Copy pins array from user space (using a scratch buffer as an intermediate)
			unsigned pinCount=hwDeviceTypeGetPinCount(type);
			if (!procManProcessMemoryReadBlock(process, procData, pinsAddr, (uint8_t *)procManScratchBuf256, pinCount, true)) {
				kernelLog(LogTypeWarning, kstrP("failed during HW device register syscall, could not read pins array %u size %u (type %u), process %u (%s), killing\n"), pinsAddr, pinCount, type, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Register the device
			procData->regs[0]=hwDeviceRegister(id, type, (const uint8_t *)procManScratchBuf256);

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
		case ByteCodeSyscallIdInt32Add16: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bValue=procData->regs[2];

			// Read 32 bit value from process memory
			BytecodeDoubleWord aValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32add16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue+=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32add16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Add32: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bPtr=procData->regs[2];

			// Read 32 bit values from process memory
			BytecodeDoubleWord aValue, bValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue) ||
			    !procManProcessMemoryReadDoubleWord(process, procData, bPtr, &bValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32add32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue+=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32add32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Sub16: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bValue=procData->regs[2];

			// Read 32 bit value from process memory
			BytecodeDoubleWord aValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32sub16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue-=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32sub16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Sub32: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bPtr=procData->regs[2];

			// Read 32 bit values from process memory
			BytecodeDoubleWord aValue, bValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue) ||
			    !procManProcessMemoryReadDoubleWord(process, procData, bPtr, &bValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32sub32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue-=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32sub32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Mul16: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bValue=procData->regs[2];

			// Read 32 bit value from process memory
			BytecodeDoubleWord aValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32mul16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue*=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32mul16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Mul32: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bPtr=procData->regs[2];

			// Read 32 bit values from process memory
			BytecodeDoubleWord aValue, bValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue) ||
			    !procManProcessMemoryReadDoubleWord(process, procData, bPtr, &bValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32mul32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue*=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32mul32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Div16: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bValue=procData->regs[2];

			// Read 32 bit value from process memory
			BytecodeDoubleWord aValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32div16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			if (bValue==0) {
				kernelLog(LogTypeWarning, kstrP("failed during int32div16 syscall, divide by 0, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			BytecodeDoubleWord quotient=aValue/bValue;
			BytecodeWord remainder=aValue-quotient*bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, quotient)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32div16 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			procData->regs[0]=remainder;

			return true;
		} break;
		case ByteCodeSyscallIdInt32Div32: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bPtr=procData->regs[2];
			BytecodeWord rPtr=procData->regs[3];

			// Read 32 bit values from process memory
			BytecodeDoubleWord aValue, bValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue) ||
			    !procManProcessMemoryReadDoubleWord(process, procData, bPtr, &bValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32div32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			if (bValue==0) {
				kernelLog(LogTypeWarning, kstrP("failed during int32div32 syscall, divide by 0, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			BytecodeDoubleWord quotient=aValue/bValue;
			BytecodeDoubleWord remainder=aValue-quotient*bValue;

			// Write results back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, quotient) ||
			    !procManProcessMemoryWriteDoubleWord(process, procData, rPtr, remainder)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32div32 syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Shl: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bValue=procData->regs[2];

			// Read 32 bit value from process memory
			BytecodeDoubleWord aValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32shl syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue<<=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32shl syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
		case ByteCodeSyscallIdInt32Shr: {
			// Grab arguments
			BytecodeWord aPtr=procData->regs[1];
			BytecodeWord bValue=procData->regs[2];

			// Read 32 bit value from process memory
			BytecodeDoubleWord aValue;
			if (!procManProcessMemoryReadDoubleWord(process, procData, aPtr, &aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32shr syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			// Compute result
			aValue>>=bValue;

			// Write result back into process memory
			if (!procManProcessMemoryWriteDoubleWord(process, procData, aPtr, aValue)) {
				kernelLog(LogTypeWarning, kstrP("failed during int32shr syscall, process %u (%s), killing\n"), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
				return false;
			}

			return true;
		} break;
	}

	kernelLog(LogTypeWarning, kstrP("invalid syscall id=%i, process %u (%s), killing\n"), syscallId, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	return false;
}

STATICASSERT(sizeof(ProcManProcessProcData)<KernelFsPathMax); // This is due to using one of the path scratch buffers to hold child's procData temporarily
void procManProcessFork(ProcManProcess *parent, ProcManProcessProcData *procData) {
	assert(parent!=NULL);
	assert(procData!=NULL);

#define scratchPath procManScratchBufPath0
#define childProcData ((ProcManProcessProcData *)procManScratchBufPath1)

	ProcManPid parentPid=procManGetPidFromProcess(parent);

	kernelLog(LogTypeInfo, kstrP("fork request from process %u\n"), parentPid);

	// Initialise child's proc file (do this now to make error handling simpler)
	*childProcData=*procData;

	childProcData->ramFd=KernelFsFdInvalid;
	childProcData->regs[0]=0; // indicate success in the child
	for(ProcManLocalFd localFd=1; localFd<ProcManMaxFds; ++localFd)
		childProcData->fds[localFd-1]=KernelFsFdInvalid;

	// Find a PID for the new process
	ProcManPid childPid=procManFindUnusedPid();
	if (childPid==ProcManPidMax) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - no spare PIDs\n"), parentPid);
		goto error;
	}
	ProcManProcess *child=&(procManData.processes[childPid]);

	// Clear process struct as required
	child->state=ProcManProcessStateUnused;
	child->progmemFd=KernelFsFdInvalid;
	child->procFd=KernelFsFdInvalid;
	child->instructionCounter=0;
#ifndef ARDUINO
	memset(child->profilingCounts, 0, sizeof(child->profilingCounts[0])*BytecodeMemoryProgmemSize);
#endif

	// Create and open proc file
	sprintf(scratchPath, "/tmp/proc%u", childPid);
	if (!kernelFsFileCreateWithSize(scratchPath, sizeof(ProcManProcessProcData))) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not create child process data file at '%s' of size %u\n"), parentPid, scratchPath, sizeof(procManProcessLoadProcData));
		goto error;
	}

	child->procFd=kernelFsFileOpen(scratchPath, KernelFsFdModeRW);
	if (child->procFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not open child process data file at '%s'\n"), parentPid, scratchPath);
		goto error;
	}

	// Attempt to create and open ram file
	sprintf(scratchPath, "/tmp/ram%u", childPid);
	uint16_t ramTotalSize=procData->envVarDataLen+procData->ramLen;
	if (!kernelFsFileCreateWithSize(scratchPath, ramTotalSize)) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not create child ram data file at '%s' of size %u\n"), parentPid, scratchPath, ramTotalSize);
		goto error;
	}

	childProcData->ramFd=kernelFsFileOpen(scratchPath, KernelFsFdModeRW);
	if (childProcData->ramFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not open child ram data file at '%s'\n"), parentPid, scratchPath);
		goto error;
	}

	// Try to use same FD as parent for the program data
	// but if this fails then try to open again with new global fd.
	child->state=ProcManProcessStateActive;

	child->progmemFd=kernelFsFileDupeOrOpen(procManData.processes[parentPid].progmemFd);
	if (child->progmemFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not reopen progmem fd %u\n"), parentPid, procManData.processes[parentPid].progmemFd);
		goto error;
	}

	// Duplicate any open file descriptors
	for(ProcManLocalFd localFd=1; localFd<ProcManMaxFds; ++localFd) {
		// No file open in this slot?
		if (procData->fds[localFd-1]==KernelFsFdInvalid)
			continue;

		// There is a file open here, so try to re-open it in the new process.
		KStr scratchPathK=kernelFsGetFilePath(procData->fds[localFd-1]);
		assert(!kstrIsNull(scratchPathK));
		kstrStrcpy(scratchPath, scratchPathK);

		childProcData->fds[localFd-1]=kernelFsFileDupeOrOpen(procData->fds[localFd-1]);
		if (childProcData->fds[localFd-1]==KernelFsFdInvalid) {
			kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not reopen '%s' (local fd %u, original global fd %u)\n"), parentPid, scratchPath, localFd, procData->fds[localFd-1]);
			goto error;
		}
	}

	// Save completed child proc data to disk
	if (!procManProcessStoreProcData(child, childProcData)) {
		sprintf(scratchPath, "/tmp/proc%u", childPid);
		kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not save child process data file to '%s'\n"), parentPid, scratchPath);
		goto error;
	}

	// Copy parent's ram into child's
	// Use a scratch buffer to copy up to 256 bytes at a time.
	KernelFsFileOffset i;
	for(i=0; i+255<ramTotalSize; i+=256) {
		if (kernelFsFileReadOffset(procData->ramFd, i, (uint8_t *)procManScratchBuf256, 256)!=256 ||
		    kernelFsFileWriteOffset(childProcData->ramFd, i, (const uint8_t *)procManScratchBuf256, 256)!=256) {
			kernelLog(LogTypeWarning, kstrP("could not fork from %u - could not copy parent's RAM into child's (managed %u/%u)\n"), parentPid, i, ramTotalSize);
			goto error;
		}
	}
	for(; i<ramTotalSize; i++) {
		uint8_t value;
		if (kernelFsFileReadOffset(procData->ramFd, i, &value, 1)!=1 ||
		    kernelFsFileWriteOffset(childProcData->ramFd, i, &value, 1)!=1) {
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
		for(ProcManLocalFd localFd=1; localFd<ProcManMaxFds; ++localFd)
			kernelFsFileClose(childProcData->fds[localFd-1]);

		kernelFsFileClose(procManData.processes[childPid].progmemFd);
		procManData.processes[childPid].progmemFd=KernelFsFdInvalid;

		kernelFsFileClose(procManData.processes[childPid].procFd);
		procManData.processes[childPid].procFd=KernelFsFdInvalid;
		sprintf(scratchPath, "/tmp/proc%u", childPid);
		kernelFsFileDelete(scratchPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us

		kernelFsFileClose(childProcData->ramFd);
		sprintf(scratchPath, "/tmp/ram%u", childPid);
		kernelFsFileDelete(scratchPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us

		procManData.processes[childPid].state=ProcManProcessStateUnused;
		procManData.processes[childPid].instructionCounter=0;
	}

	// Indicate error
	procData->regs[0]=ProcManPidMax;
#undef scratchPath
#undef childProcData
}

bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData) {
	assert(process!=NULL);
	assert(procData!=NULL);

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
	assert(process!=NULL);
	assert(procData!=NULL);

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
		if (!procManProcessGetArgvNStr(process, procData, start+i, argv+argvOffset)) {
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
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(argv!=NULL);

#define tempPwd procManScratchBufPath0
#define tempPath procManScratchBufPath1
#define ramPath procManScratchBufPath2

	// Write to log
	kernelLog(LogTypeInfo, kstrP("exec in %u - argv:"), procManGetPidFromProcess(process), argc);
	for(int i=0; i<argc; ++i) {
		const char *arg=procManArgvStringGetArgN(argc, argv, i);
		kernelLogAppend(LogTypeInfo, kstrP(" [%u]='%s'"), i, arg);
	}
	kernelLogAppend(LogTypeInfo, kstrP("\n"));

	// Grab pwd and path env vars as these may now point into general ram, which is about to be cleared when we resize
	if (!procManProcessMemoryReadStr(process, procData, procData->pwd, tempPwd, KernelFsPathMax)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not read env var pwd at addr %u\n"), procManGetPidFromProcess(process), procData->pwd);
		return false;
	}
	kernelFsPathNormalise(tempPwd);

	if (!procManProcessMemoryReadStr(process, procData, procData->path, tempPath, KernelFsPathMax)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not read env var path at addr %u\n"), procManGetPidFromProcess(process), procData->path);
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

	// Close old fd
	ProcManPid pid=procManGetPidFromProcess(process);
	kernelFsFileClose(procManData.processes[pid].progmemFd);
	procManData.processes[pid].progmemFd=newProgmemFd;

	// Reset instruction pointer
	procData->regs[BytecodeRegisterIP]=0;

	// Update env var array and calculate new len
	procData->envVarDataLen=0;

	procData->argc=argc;
	assert(procData->envVarDataLen==0); // argv is always at offset=0 so we do not have to store its address
	procData->envVarDataLen+=argvTotalSize;

	procData->pwd=ProcManEnvVarsVirtualOffset+procData->envVarDataLen;
	procData->envVarDataLen+=strlen(tempPwd)+1;
	procData->path=ProcManEnvVarsVirtualOffset+procData->envVarDataLen;
	procData->envVarDataLen+=strlen(tempPath)+1;

	// Resize ram file (clear data and add new arguments)
	procData->ramLen=0; // no ram needed initially
	uint16_t newRamTotalSize=procData->envVarDataLen+procData->ramLen;

	sprintf(ramPath, "/tmp/ram%u", pid);

	kernelFsFileClose(procData->ramFd);
	if (!kernelFsFileResize(ramPath, newRamTotalSize)) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not resize new processes RAM file at '%s' to %u\n"), procManGetPidFromProcess(process), ramPath, newRamTotalSize);
		return false;
	}
	procData->ramFd=kernelFsFileOpen(ramPath, KernelFsFdModeRW);
	assert(procData->ramFd!=KernelFsFdInvalid);

	// Write env vars into ram file
	if (kernelFsFileWriteOffset(procData->ramFd, 0, (const uint8_t *)(argv), argvTotalSize)!=argvTotalSize) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not write argv into new processes memory\n"), procManGetPidFromProcess(process));
		return false;
	}

	if (kernelFsFileWriteOffset(procData->ramFd, procData->pwd-ProcManEnvVarsVirtualOffset, (const uint8_t *)(tempPwd), strlen(tempPwd)+1)!=strlen(tempPwd)+1) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not write env var pwd into new processes memory\n"), procManGetPidFromProcess(process));
		return false;
	}

	if (kernelFsFileWriteOffset(procData->ramFd, procData->path-ProcManEnvVarsVirtualOffset, (const uint8_t *)(tempPath), strlen(tempPath)+1)!=strlen(tempPath)+1) {
		kernelLog(LogTypeWarning, kstrP("exec in %u failed - could not write env var path into new processes memory\n"), procManGetPidFromProcess(process));
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
	assert(argc!=NULL);
	assert(argvStart!=NULL);

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
		newProgmemFd=kernelFsFileOpen(loopExecFile, KernelFsFdModeRO);
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
			kernelLog(LogTypeInfo, kstrP("loading executable in %u - magic bytes '//' detected, assuming native executable (prev exec path '%s', fd %u)\n"), procManGetPidFromProcess(process), loopExecFile, newProgmemFd);
			break;
		} else if (magicBytes[0]=='#' && magicBytes[1]=='!') {
			// An interpreter (with path following after '#!') should be used instead to run this file.

			// Read interpreter path string
			char interpreterPath[KernelFsPathMax];
			KernelFsFileOffset readCount=kernelFsFileReadOffset(newProgmemFd, 2, (uint8_t *)interpreterPath, KernelFsPathMax);
			interpreterPath[readCount-1]='\0';

			// Look for newline and if found terminate string here
			char *newlinePtr=strchr(interpreterPath, '\n');
			if (newlinePtr==NULL) {
				kernelLog(LogTypeWarning, kstrP("loading executable in %u failed - '#!' not followed by interpreter path (prev exec path '%s', fd %u)\n"), procManGetPidFromProcess(process), loopExecFile, newProgmemFd);
				kernelFsFileClose(newProgmemFd);
				return KernelFsFdInvalid;
			}
			*newlinePtr='\0';

			// Write to log
			kernelFsPathNormalise(interpreterPath);
			kernelLog(LogTypeInfo, kstrP("loading executable in %u - magic bytes '#!' detected, using interpreter '%s' (prev exec path '%s', fd %u)\n"), procManGetPidFromProcess(process), interpreterPath, loopExecFile, newProgmemFd);

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
	assert(process!=NULL);
	assert(procData!=NULL);

	BytecodeWord offset=procData->regs[2];
	return procManProcessReadCommon(process, procData, offset);
}

bool procManProcessRead32(ProcManProcess *process, ProcManProcessProcData *procData) {
	assert(process!=NULL);
	assert(procData!=NULL);

	uint16_t offsetPtr=procData->regs[2];
	BytecodeDoubleWord offset;
	if (!procManProcessMemoryReadDoubleWord(process, procData, offsetPtr, &offset))
		return false;

	return procManProcessReadCommon(process, procData, offset);
}

bool procManProcessReadCommon(ProcManProcess *process, ProcManProcessProcData *procData, KernelFsFileOffset offset) {
	assert(process!=NULL);
	assert(procData!=NULL);

	// Grab read parameters
	ProcManLocalFd localFd=procData->regs[1];
	uint16_t bufAddr=procData->regs[3];
	BytecodeWord len=procData->regs[4];

	// Grab global fd from local one
	KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
	if (globalFd==KernelFsFdInvalid)
		return false;

	// Read a block at a time
	BytecodeWord i=0;
	while(i<len) {
		BytecodeWord chunkSize=len-i;
		if (chunkSize>256)
			chunkSize=256;

		BytecodeWord read=kernelFsFileReadOffset(globalFd, offset+i, (uint8_t *)procManScratchBuf256, chunkSize);
		if (read==0)
			break;

		if (!procManProcessMemoryWriteBlock(process, procData, bufAddr+i, (const uint8_t *)procManScratchBuf256, read))
			return false;

		i+=read;
		if (read<chunkSize)
			break;
	}

	// Update r0 to indicate how many bytes were read
	procData->regs[0]=i;

	return true;
}

bool procManProcessWrite(ProcManProcess *process, ProcManProcessProcData *procData) {
	assert(process!=NULL);
	assert(procData!=NULL);

	uint16_t offset=procData->regs[2];
	return procManProcessWriteCommon(process, procData, offset);
}

bool procManProcessWrite32(ProcManProcess *process, ProcManProcessProcData *procData) {
	assert(process!=NULL);
	assert(procData!=NULL);

	uint16_t offsetPtr=procData->regs[2];
	KernelFsFileOffset offset;
	if (!procManProcessMemoryReadDoubleWord(process, procData, offsetPtr, &offset))
		return false;

	return procManProcessWriteCommon(process, procData, offset);
}

bool procManProcessWriteCommon(ProcManProcess *process, ProcManProcessProcData *procData, KernelFsFileOffset offset) {
	assert(process!=NULL);
	assert(procData!=NULL);

	// Grab write parameters
	ProcManLocalFd localFd=procData->regs[1];
	uint16_t bufAddr=procData->regs[3];
	KernelFsFileOffset len=procData->regs[4];

	// Grab global fd from local one
	KernelFsFd globalFd=procManProcessGetGlobalFdFromLocal(process, procData, localFd);
	if (globalFd==KernelFsFdInvalid)
		return false;

	// Write up to a block at a time
	KernelFsFileOffset i=0;
	while(i<len) {
		KernelFsFileOffset chunkSize=len-i;
		if (chunkSize>256)
			chunkSize=256;

		if (!procManProcessMemoryReadBlock(process, procData, bufAddr+i, (uint8_t *)procManScratchBuf256, chunkSize, true))
			return false;
		KernelFsFileOffset written=kernelFsFileWriteOffset(globalFd, offset+i, (const uint8_t *)procManScratchBuf256, chunkSize);
		i+=written;
		if (written<chunkSize)
			break;
	}

	// Update r0 to indicate number of bytes written
	procData->regs[0]=i;

	return true;
}

ProcManLocalFd procManProcessOpenFile(ProcManProcess *process, ProcManProcessProcData *procData, const char *path, KernelFsFdMode mode) {
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(path!=NULL);

	// Ensure mode is sensible
	if (mode==KernelFsFdModeNone || mode>=KernelFsFdModeMax) {
		kernelLog(LogTypeWarning, kstrP("failed to open file: path '%s', bad mode %u, process %u (%s)\n"), path, mode, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return ProcManLocalFdInvalid;
	}

	// Ensure process has a spare slot in the fds table
	ProcManLocalFd localFd;
	for(localFd=1; localFd<ProcManMaxFds; ++localFd)
		if (procData->fds[localFd-1]==KernelFsFdInvalid)
			break;

	if (localFd==ProcManMaxFds-1) {
		kernelLog(LogTypeWarning, kstrP("failed to open file: path '%s', mode %s, fds table full, process %u (%s)\n"), path, kernelFsFdModeToString(mode), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return ProcManLocalFdInvalid;
	}

	// Attempt to open file (and if successful add to fds table)
	procData->fds[localFd-1]=kernelFsFileOpen(path, mode);
	if (procData->fds[localFd-1]==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("failed to open file: path '%s', mode %s, open failed, process %u (%s)\n"), path, kernelFsFdModeToString(mode), procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return ProcManLocalFdInvalid;
	}

	// Write to log.
	kernelLog(LogTypeInfo, kstrP("openned userspace file: path '%s', mode %s, local fd %u, global fd %u, process %u (%s)\n"), path, kernelFsFdModeToString(mode), localFd, procData->fds[localFd-1], procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));

	return localFd;
}

void procManProcessCloseFile(ProcManProcess *process, ProcManProcessProcData *procData, ProcManLocalFd localFd) {
	assert(process!=NULL);
	assert(procData!=NULL);

	// Special case for invalid fd (must be null-op)
	if (localFd==ProcManLocalFdInvalid)
		return;

	// Fd out of range?
	if (localFd>=ProcManMaxFds) {
		kernelLog(LogTypeWarning, kstrP("close syscall, local fd %u outside of valid range, process %u (%s)\n"), localFd ,procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return;
	}

	// Is there actually an open file in the fd table for the given fd?
	if (procData->fds[localFd-1]==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not close file, local fd %u is unused, process %u (%s)\n"), localFd ,procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return;
	}

	// Grab path first for logging, before we close the file and lose it.
	KStr pathKStr=kernelFsGetFilePath(procData->fds[localFd-1]);
	if (kstrIsNull(pathKStr)) {
		kernelLog(LogTypeWarning, kstrP("could not close file, global fd %u not open (local fd %u), process %u (%s)\n"), procData->fds[localFd-1], localFd, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		procData->fds[localFd-1]=KernelFsFdInvalid; // might as well remove for safety
		return;
	}

	char path[KernelFsPathMax];
	kstrStrcpy(path, pathKStr);

	// Close file
	unsigned newRefCount=kernelFsFileClose(procData->fds[localFd-1]);

	// Special case - is this a pipe file which now has no more references?
	unsigned pipeId=(kstrStrncmp(path, kstrP("/dev/pipe"), 9)==0 ? atoi(path+9) : 0);
	if (pipeId>0 && !kernelFsFileIsOpen(path)) {
		// Unmount circular buffer
		kernelUnmount(path);

		// Delete backing file
		char backingPath[KernelFsPathMax];
		sprintf(backingPath, "/tmp/pipe%u", pipeId);
		kernelFsFileDelete(backingPath);
	}

	// Write to log
	if (pipeId>0)
		kernelLog(LogTypeInfo, kstrP("closed userspace reference to pipe file '%s', local fd %u, global fd %u, new ref count %u, process %u (%s)\n"), path, localFd, procData->fds[localFd-1], newRefCount, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
	else
		kernelLog(LogTypeInfo, kstrP("closed userspace reference to file '%s', local fd %u, global fd %u, new ref count %u, process %u (%s)\n"), path, localFd, procData->fds[localFd-1], newRefCount, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));

	// Remove from fd table
	procData->fds[localFd-1]=KernelFsFdInvalid;
}

KernelFsFd procManProcessGetGlobalFdFromLocal(ProcManProcess *process, ProcManProcessProcData *procData, ProcManLocalFd localFd) {
	assert(process!=NULL);
	assert(procData!=NULL);

	// Bad localFd?
	if (localFd==ProcManLocalFdInvalid || localFd>=ProcManMaxFds)
		return KernelFsFdInvalid;

	// Lookup global fd in table
	KernelFsFd globalFd=procData->fds[localFd-1];

	// Check for bad fd globally
	// TODO: should we terminate the process if this check fails? it suggests that the fds array is corrupt
	if (!kernelFsFileIsOpenByFd(globalFd))
		return KernelFsFdInvalid;

	assert(globalFd!=KernelFsFdInvalid && globalFd<KernelFsFdMax);
	return globalFd;
}

KernelFsFd procManProcessGetGlobalFdFromLocalWithPid(ProcManPid pid, ProcManLocalFd localFd) {
	assert(pid<ProcManPidMax);
	assert(localFd<ProcManMaxFds);

	// Invalid local fd?
	if (localFd==ProcManLocalFdInvalid)
		return KernelFsFdInvalid;

	// Grab process (if exists)
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return KernelFsFdInvalid;

	// Load proc data
	ProcManProcessProcData procData;
	if (!procManProcessLoadProcData(process, &procData))
		return KernelFsFdInvalid;

	// Lookup in fds table
	return procData.fds[localFd-1];
}

void procManResetInstructionCounters(void) {
	for(ProcManPid i=0; i<ProcManPidMax; ++i)
		procManData.processes[i].instructionCounter=0;
}

void procManProcessDebug(ProcManProcess *process, ProcManProcessProcData *procData) {
	assert(process!=NULL);
	assert(procData!=NULL);

	// Simply print PID and register values
	kernelLog(LogTypeInfo, kstrP("Process %u debug: r0=%u, r1=%u, r2=%u, r3=%u, r4=%u, r5=%u, r6=%u, r7=%u\n"), procManGetPidFromProcess(process), procData->regs[0], procData->regs[1], procData->regs[2], procData->regs[3], procData->regs[4], procData->regs[5], procData->regs[6], procData->regs[7]);
}

char *procManArgvStringGetArgN(uint8_t argc, char *argvStart, uint8_t n) {
	assert(argvStart!=NULL);

	// Check n is in range
	if (n>=argc)
		return NULL;

	// Look for argument by looping over all that come before it.
	while(n>0)
		n-=(*argvStart++=='\0');

	return argvStart;
}

const char *procManArgvStringGetArgNConst(uint8_t argc, const char *argvStart, uint8_t n) {
	assert(argvStart!=NULL);

	// Check n is in range
	if (n>=argc)
		return NULL;

	// Look for argument by looping over all that come before it.
	while(n>0)
		n-=(*argvStart++=='\0');

	return argvStart;
}

int procManArgvStringGetTotalSize(uint8_t argc, const char *argvStart) {
	assert(argvStart!=NULL);

	int len=0;
	while(argc>0)
		argc-=(argvStart[len++]=='\0');
	return len;
}

void procManArgvStringPathNormaliseArg0(uint8_t argc, char *argvStart) {
	assert(argvStart!=NULL);

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
	assert(argc!=NULL);
	assert(argvStart!=NULL);
	assert(interpreterPath!=NULL);

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
	assert(pd!=NULL);

	pd->len=0;
}

bool procManPrefetchDataReadByte(ProcManPrefetchData *pd, ProcManProcess *process, ProcManProcessProcData *procData, uint16_t addr, uint8_t *value) {
	assert(pd!=NULL);
	assert(process!=NULL);
	assert(procData!=NULL);
	assert(value!=NULL);

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
	assert(argvStart!=NULL);

	kernelLog(LogTypeInfo, kstrP("Argv Debug: argc=%u, arg0='%s'\n"), argc, argvStart);
	for(int i=1; i<argc; ++i) {
		const char *arg=procManArgvStringGetArgNConst(argc, argvStart, i);
		kernelLog(LogTypeInfo, kstrP("Argv Debug:         arg%u='%s'\n"), i, arg);
	}
}
