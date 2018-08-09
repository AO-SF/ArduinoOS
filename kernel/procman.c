#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "debug.h"
#include "kernel.h"
#include "kernelfs.h"
#include "procman.h"
#include "wrapper.h"

#define procManProcessInstructionCounterMax ((1u)<<16) // TODO: On arduino this needs 32 bit
#define procManProcessInstructionCounterMaxMinusOne (((1u)<<16)-1) // TODO: On arduino this only needs 16 bit but needs calculating differently
#define procManProcessTickInstructionsPerTick 8 // Generally a higher value causes faster execution, but decreased responsiveness if many processes running
#define procManTicksPerInstructionCounterReset (3*1024) // must not exceed procManProcessInstructionCounterMax/procManProcessTickInstructionsPerTick, which is currently 8k, target is to reset roughly every 10s

typedef enum {
	ProcManProcessStateUnused,
	ProcManProcessStateActive,
	ProcManProcessStateWaitingWaitpid,
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
bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong instruction);

void procManProcessFork(ProcManProcess *process, ProcManProcessProcData *procData);
bool procManProcessExec(ProcManProcess *process, ProcManProcessProcData *procData); // Returns false only on critical error (e.g. segfault), i.e. may return true even though exec operation itself failed

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
		procManProcessKill(i);
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

	// Find a PID for the new process
	ProcManPid pid=procManFindUnusedPid();
	if (pid==ProcManPidMax)
		return ProcManPidMax;

	// Construct tmp paths
	// TODO: Try others if exist
	char procPath[KernelFsPathMax], ramPath[KernelFsPathMax];
	sprintf(procPath, "/tmp/proc%u", pid);
	sprintf(ramPath, "/tmp/ram%u", pid);

	// Attempt to open program file
	procManData.processes[pid].progmemFd=kernelFsFileOpen(programPath);
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid)
		goto error;

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(procPath, sizeof(ProcManProcessProcData)))
		goto error;
	KernelFsFileOffset argvDataLen=1; // single byte for NULL terminator acting as all (empty) arguments
	if (!kernelFsFileCreateWithSize(ramPath, argvDataLen))
		goto error;

	// Attempt to open proc and ram files
	procManData.processes[pid].procFd=kernelFsFileOpen(procPath);
	if (procManData.processes[pid].procFd==KernelFsFdInvalid)
		goto error;

	ramFd=kernelFsFileOpen(ramPath);
	if (ramFd==KernelFsFdInvalid)
		goto error;

	// Initialise state
	procManData.processes[pid].state=ProcManProcessStateActive;
	procManData.processes[pid].instructionCounter=0;

	// Initialise proc file (and argv data in ram file)
	ProcManProcessProcData procData;
	procData.regs[ByteCodeRegisterIP]=0;
	procData.skipFlag=false;
	procData.envVars.stdioFd=KernelFsFdInvalid;
	procData.argvDataLen=argvDataLen;
	procData.ramLen=0;
	procData.ramFd=ramFd;
	strcpy(procData.envVars.pwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(procData.envVars.pwd, &dirname, &basename);
	assert(dirname==procData.envVars.pwd);

	strcpy(procData.envVars.path, "/bin");

	uint8_t nullByte;
	if (kernelFsFileWriteOffset(ramFd, 0, &nullByte, 1)!=1)
		goto error;
	for(unsigned i=0; i<ARGVMAX; ++i)
		procData.envVars.argv[i]=0;

	if (!procManProcessStoreProcData(&procManData.processes[pid], &procData))
		goto error;

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

void procManProcessKill(ProcManPid pid) {
	// Not even open?
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return;

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

	// Check if any processes are waiting due to waitpid syscall
	for(unsigned i=0; i<ProcManPidMax; ++i) {
		if (procManData.processes[i].state==ProcManProcessStateWaitingWaitpid && procManData.processes[i].waitingData8==pid) {
			// Bring this process back to life
			procManData.processes[i].state=ProcManProcessStateActive;
		}
	}
}

void procManProcessTick(ProcManPid pid) {
	// Find process from PID
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return;

	// Is this process waiting for a timeout, and that time has been reached?
	if (process->state==ProcManProcessStateWaitingWaitpid && process->waitingData16>0 && millis()/1000>=process->waitingData16)
		process->state=ProcManProcessStateActive;

	// Is this process not even active?
	if (procManData.processes[pid].state!=ProcManProcessStateActive)
		return;

	// Load proc data
	ProcManProcessProcData procData;
	bool res=procManProcessLoadProcData(process, &procData);
	assert(res);

	// Run a few instructions
	for(unsigned instructionNum=0; instructionNum<procManProcessTickInstructionsPerTick; ++instructionNum) {
		// Run a single instruction
		BytecodeInstructionLong instruction;
		if (!procManProcessGetInstruction(process, &procData, &instruction))
			goto kill;

		// Are we meant to skip this instruction? (due to a previous skipN instruction)
		if (procData.skipFlag) {
			procData.skipFlag=false;

			// Read the next instruction instead
			if (!procManProcessGetInstruction(process, &procData, &instruction))
				goto kill;
		}

		// Execute instruction
		if (!procManProcessExecInstruction(process, &procData, instruction))
			goto kill;

		// Increment instruction counter
		assert(process->instructionCounter<procManProcessInstructionCounterMaxMinusOne); // we reset often enough to prevent this
		++process->instructionCounter;

		// Has this process gone inactive?
		if (procManData.processes[pid].state!=ProcManProcessStateActive)
			break;
	}

	// Save tmp data
	if (!procManProcessStoreProcData(process, &procData))
		assert(false);
	return;

	kill:
	procManProcessKill(pid);
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
			debugLog("warning: process %u (%s) tried to read invalid address (0x%04X, pointing to PROGMEM at offset %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, addr);
			return false;
		}
	} else {
		// Address is in RAM
		ByteCodeWord ramIndex=(addr-ByteCodeMemoryRamAddr);
		if (ramIndex<procData->ramLen) {
			bool res=kernelFsFileReadOffset(procData->ramFd, procData->argvDataLen+ramIndex, value, 1, false);
			assert(res);
			return true;
		} else {
			debugLog("warning: process %u (%s) tried to read invalid address (0x%04X, pointing to RAM at offset %u, but size is only %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, procData->ramLen);
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
		debugLog("warning: process %u (%s) tried to write to read-only address (0x%04X), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr);
		return false;
	}

	// addr is in RAM
	ByteCodeWord ramIndex=(addr-ByteCodeMemoryRamAddr);
	if (ramIndex<procData->ramLen) {
		bool res=kernelFsFileWriteOffset(procData->ramFd, procData->argvDataLen+ramIndex, &value, 1);
		assert(res);
		return true;
	} else {
		// Close ram file
		char ramFdPath[KernelFsPathMax];
		strcpy(ramFdPath, kernelFsGetFilePath(procData->ramFd));
		kernelFsFileClose(procData->ramFd);

		// Resize ram file
		uint16_t newRamLen=ramIndex+1;
		uint16_t newRamTotalSize=procData->argvDataLen+newRamLen;
		if (!kernelFsFileResize(ramFdPath, newRamTotalSize)) {
			return false;
		}

		// Re-open ram file
		procData->ramFd=kernelFsFileOpen(ramFdPath);
		if (procData->ramFd==KernelFsFdInvalid)
			return false; // TODO: tidy up better?

		// Update stored ram len and write byte
		procData->ramLen=newRamLen;

		bool res=kernelFsFileWriteOffset(procData->ramFd, procData->argvDataLen+ramIndex, &value, 1);
		assert(res);

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
			debugLog("warning: corrupt argvdata or ram file more generally? Process %u (%s), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
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

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessProcData *procData, BytecodeInstructionLong instruction) {
	// Parse instruction
	BytecodeInstructionInfo info;
	if (!bytecodeInstructionParse(&info, instruction)) {
		debugLog("warning: could not parse instruction 0x%02X%02X%02X, process %u (%s), killing\n", instruction[0], instruction[1], instruction[2], procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
		return false;
	}

	// Execute instruction
	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore8:
					if (!procManProcessMemoryWriteByte(process, procData, procData->regs[info.d.memory.destReg], procData->regs[info.d.memory.srcReg]))
						return false;
				break;
				case BytecodeInstructionMemoryTypeLoad8: {
					uint8_t value;
					if (!procManProcessMemoryReadByte(process, procData, procData->regs[info.d.memory.srcReg], &value))
						return false;
					procData->regs[info.d.memory.destReg]=value;
				} break;
				case BytecodeInstructionMemoryTypeReserved:
					return false;
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
					if (!procManProcessMemoryWriteWord(process, procData, destAddr, opA))
						return false;
				} break;
				case BytecodeInstructionAluTypeLoad16: {
					ByteCodeWord srcAddr=procData->regs[info.d.alu.opAReg];
					if (!procManProcessMemoryReadWord(process, procData, srcAddr, &procData->regs[info.d.alu.destReg]))
						return false;
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
							return false; // TODO: pass on exit status
						break;
						case ByteCodeSyscallIdGetPid:
							procData->regs[0]=procManGetPidFromProcess(process);
						break;
						case ByteCodeSyscallIdGetArgC: {
							procData->regs[0]=0;
							for(unsigned i=0; i<ARGVMAX; ++i) {
								char arg[64]; // TODO: Avoid hardcoded limit
								if (!procManProcessGetArgvN(process, procData, i, arg))
									return false;
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
								if (!procManProcessGetArgvN(process, procData, n, arg))
									return false;
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, arg))
									return false;
								procData->regs[0]=strlen(arg);
							}
						} break;
						case ByteCodeSyscallIdFork:
							procManProcessFork(process, procData);
						break;
						case ByteCodeSyscallIdExec:
							if (!procManProcessExec(process, procData))
								return false;
						break;
						case ByteCodeSyscallIdWaitPid: {
							ByteCodeWord waitPid=procData->regs[1];
							ByteCodeWord timeout=procData->regs[2];

							// If given pid does not represent a process, return immediately
							if (procManGetProcessByPid(waitPid)!=NULL) {
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
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, execPath))
									return false;
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
								}
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, str))
									return false;
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
								if (!procManProcessMemoryWriteWord(process, procData, bufAddr, value))
									return false;
								bufAddr+=2;
							}
						} break;
						case ByteCodeSyscallIdKill: {
							ByteCodeWord pid=procData->regs[1];
							if (pid!=0) // do not allow killing init
								procManProcessKill(pid);
						} break;
						case ByteCodeSyscallIdGetPidRam: {
							ByteCodeWord pid=procData->regs[1];
							ProcManProcess *qProcess=procManGetProcessByPid(pid);
							if (qProcess!=NULL) {
								ProcManProcessProcData qProcData;
								bool res=procManProcessLoadProcData(qProcess, &qProcData);
								assert(res);
								procData->regs[0]=sizeof(ProcManProcessProcData)+qProcData.argvDataLen+qProcData.ramLen;
							} else
								procData->regs[0]=0;
						} break;
						case ByteCodeSyscallIdRead: {
							KernelFsFd fd=procData->regs[1];
							uint16_t offset=procData->regs[2];
							uint16_t bufAddr=procData->regs[3];
							KernelFsFileOffset len=procData->regs[4];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value;
								if (kernelFsFileReadOffset(fd, offset+i, &value, 1, true)!=1)
									break;
								if (!procManProcessMemoryWriteByte(process, procData, bufAddr+i, value))
									return false;
							}
							procData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdWrite: {
							KernelFsFd fd=procData->regs[1];
							uint16_t offset=procData->regs[2];
							uint16_t bufAddr=procData->regs[3];
							KernelFsFileOffset len=procData->regs[4];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value;
								if (!procManProcessMemoryReadByte(process, procData, bufAddr+i, &value))
									return false;
								if (kernelFsFileWriteOffset(fd, offset+i, &value, 1)!=1)
									break;
							}
							procData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdOpen: {
							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], path, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(path);
							procData->regs[0]=kernelFsFileOpen(path);
						} break;
						case ByteCodeSyscallIdClose:
							kernelFsFileClose(procData->regs[1]);
						break;
						case ByteCodeSyscallIdDirGetChildN: {
							KernelFsFd fd=procData->regs[1];
							ByteCodeWord childNum=procData->regs[2];
							uint16_t bufAddr=procData->regs[3];

							char childPath[KernelFsPathMax];
							bool result=kernelFsDirectoryGetChild(fd, childNum, childPath);

							if (result) {
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, childPath))
									return false;
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
								if (!procManProcessMemoryWriteStr(process, procData, bufAddr, srcPath))
									return false;
								procData->regs[0]=1;
							}
						} break;
						case ByteCodeSyscallIdResizeFile: {
							// Grab path and new size
							uint16_t pathAddr=procData->regs[1];
							KernelFsFileOffset newSize=procData->regs[2];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax))
								return false;
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
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(path);
							procData->regs[0]=kernelFsFileGetLen(path);
						} break;
						case ByteCodeSyscallIdTryReadByte: {
							KernelFsFd fd=procData->regs[1];

							uint8_t value;
							if (kernelFsFileReadOffset(fd, 0, &value, 1, false)==1)
								procData->regs[0]=value;
							else
								procData->regs[0]=256;
						} break;
						case ByteCodeSyscallIdIsDir: {
							uint16_t pathAddr=procData->regs[1];

							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, procData, pathAddr, path, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(path);
							procData->regs[0]=kernelFsFileIsDir(path);
						} break;
						case ByteCodeSyscallIdEnvGetStdioFd:
							procData->regs[0]=procData->envVars.stdioFd;
						break;
						case ByteCodeSyscallIdEnvSetStdioFd:
							procData->envVars.stdioFd=procData->regs[1];
						break;
						case ByteCodeSyscallIdEnvGetPwd:
							if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], procData->envVars.pwd))
								return false;
						break;
						case ByteCodeSyscallIdEnvSetPwd:
							if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], procData->envVars.pwd, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(procData->envVars.pwd);
						break;
						case ByteCodeSyscallIdEnvGetPath:
							if (!procManProcessMemoryWriteStr(process, procData, procData->regs[1], procData->envVars.path))
								return false;
						break;
						case ByteCodeSyscallIdEnvSetPath:
							if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], procData->envVars.path, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(procData->envVars.path);
						break;
						case ByteCodeSyscallIdTimeMonotonic:
							procData->regs[0]=(millis()/1000);
						break;
						default:
							debugLog("warning: invalid syscall id=%i, process %u (%s), killing\n", syscallId, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
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

	// Find a PID for the new process
	ProcManPid childPid=procManFindUnusedPid();
	if (childPid==ProcManPidMax)
		goto error;
	ProcManProcess *child=&(procManData.processes[childPid]);

	// Construct proc file path
	// TODO: Try others if exists
	char childProcPath[KernelFsPathMax], childRamPath[KernelFsPathMax];
	sprintf(childProcPath, "/tmp/proc%u", childPid);
	sprintf(childRamPath, "/tmp/ram%u", childPid);

	// Attempt to create proc and ram files
	if (!kernelFsFileCreateWithSize(childProcPath, sizeof(ProcManProcessProcData)))
		goto error;
	uint16_t ramTotalSize=procData->argvDataLen+procData->ramLen;
	if (!kernelFsFileCreateWithSize(childRamPath, ramTotalSize))
		goto error;

	// Attempt to open proc and ram files
	child->procFd=kernelFsFileOpen(childProcPath);
	if (child->procFd==KernelFsFdInvalid)
		goto error;

	ramFd=kernelFsFileOpen(childRamPath);
	if (ramFd==KernelFsFdInvalid)
		goto error;

	// Simply use same FD as parent for the program data
	child->state=ProcManProcessStateActive;
	child->progmemFd=procManData.processes[parentPid].progmemFd;
	child->instructionCounter=0;

	// Initialise proc file
	ProcManProcessProcData childProcData=*procData;
	childProcData.ramFd=ramFd;
	childProcData.regs[0]=0; // indicate success in the child

	if (!procManProcessStoreProcData(child, &childProcData))
		goto error;

	// Copy parent's ram into child's
	for(KernelFsFileOffset i=0; i<ramTotalSize; ++i) {
		bool res=true;
		uint8_t value;
		res&=(kernelFsFileReadOffset(procData->ramFd, i, &value, 1, false)==1);
		res&=(kernelFsFileWriteOffset(childProcData.ramFd, i, &value, 1)==1);
		assert(res);
	}

	// Update parent return value with child's PID
	procData->regs[0]=childPid;

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
	// Grab path and args (if any)
	char args[ARGVMAX][64]; // TODO: Avoid hardcoded 64
	for(unsigned i=0; i<ARGVMAX; ++i)
		args[i][0]='\0';

	if (!procManProcessMemoryReadStr(process, procData, procData->regs[1], args[0], KernelFsPathMax))
		return false;

	for(unsigned i=1; i<ARGVMAX; ++i)
		if (procData->regs[i+1]!=0)
			if (!procManProcessMemoryReadStr(process, procData, procData->regs[i+1], args[i], KernelFsPathMax))
				return false;

	// Normalise path and then check if valid
	kernelFsPathNormalise(args[0]);

	if (!kernelFsPathIsValid(args[0]))
		return true;

	if (kernelFsFileIsDir(args[0]))
		return true;

	// Attempt to open new program file
	KernelFsFd newProgmemFd=kernelFsFileOpen(args[0]);
	if (newProgmemFd==KernelFsFdInvalid)
		return true;

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
	if (!kernelFsFileResize(ramPath, newRamTotalSize))
		return false;
	procData->ramFd=kernelFsFileOpen(ramPath);
	assert(procData->ramFd!=KernelFsFdInvalid);

	// Write args into ram file
	for(unsigned i=0; i<ARGVMAX; ++i) {
		uint16_t argSize=strlen(args[i])+1;
		if (kernelFsFileWriteOffset(procData->ramFd, procData->envVars.argv[i], (const uint8_t *)(args[i]), argSize)!=argSize)
			return false;
	}

	return true;
}

void procManResetInstructionCounters(void) {
	for(unsigned i=0; i<ProcManPidMax; ++i)
		procManData.processes[i].instructionCounter=0;
}
