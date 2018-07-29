#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "debug.h"
#include "kernel.h"
#include "kernelfs.h"
#include "procman.h"

#define ProcManProcessRamSize 1024 // TODO: Allow this to be dynamic

typedef enum {
	ProcManProcessStateUnused,
	ProcManProcessStateActive,
	ProcManProcessStateWaitingWaitpid,
} ProcManProcessState;

#define ARGVMAX 4
typedef struct {
	KernelFsFd stdioFd; // set to KernelFsInvalid when init is called
	char pwd[KernelFsPathMax]; // set to '/' when init is called
	char argv[ARGVMAX][64]; // TODO: Avoid hardcoded 64 limit
	char path[KernelFsPathMax]; // set to '/bin' when init is called
} ProcManProcessEnvVars;

typedef struct {
	ByteCodeWord regs[BytecodeRegisterNB];
	ProcManProcessEnvVars envVars;
	bool skipFlag; // skip next instruction?
	uint8_t ram[ProcManProcessRamSize];
} ProcManProcessTmpData;

typedef struct {
	KernelFsFd progmemFd, tmpFd;
	uint8_t state;
	uint8_t waitingData;
} ProcManProcess;

typedef struct {
	ProcManProcess processes[ProcManPidMax];
} ProcMan;
ProcMan procManData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid);
ProcManPid procManGetPidFromProcess(ProcManProcess *process);
const char *procManGetExecPathFromProcess(const ProcManProcess *process);

ProcManPid procManFindUnusedPid(void);

bool procManProcessGetTmpData(ProcManProcess *process, ProcManProcessTmpData *tmpData);

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, uint8_t *value);
bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, ByteCodeWord *value);
bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, char *str, uint16_t len);
bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, uint8_t value);
bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, ByteCodeWord value);
bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, const char *str);

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessTmpData *tmpData, BytecodeInstructionLong instruction);

void procManProcessFork(ProcManProcess *process, ProcManProcessTmpData *tmpData);
bool procManProcessExec(ProcManProcess *process, ProcManProcessTmpData *tmpData); // Returns false only on critical error (e.g. segfault), i.e. may return true even though exec operation itself failed

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void) {
	// Clear processes table
	for(int i=0; i<ProcManPidMax; ++i) {
		procManData.processes[i].state=ProcManProcessStateUnused;
		procManData.processes[i].progmemFd=KernelFsFdInvalid;
		procManData.processes[i].tmpFd=KernelFsFdInvalid;
	}
}

void procManQuit(void) {
	// Kill all processes
	for(int i=0; i<ProcManPidMax; ++i)
		procManProcessKill(i);
}

void procManTickAll(void) {
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		procManProcessTick(pid);
}

int procManGetProcessCount(void) {
	int count=0;
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		count+=(procManGetProcessByPid(pid)!=NULL);
	return count;
}

ProcManPid procManProcessNew(const char *programPath) {
	// Find a PID for the new process
	ProcManPid pid=procManFindUnusedPid();
	if (pid==ProcManPidMax)
		goto error;

	// Construct tmp file path
	// TODO: Try others if exists
	char tmpPath[KernelFsPathMax];
	sprintf(tmpPath, "/tmp/proc%u", pid);

	// Attempt to open program file
	procManData.processes[pid].progmemFd=kernelFsFileOpen(programPath);
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid)
		goto error;

	// Attempt to create tmp file
	if (!kernelFsFileCreateWithSize(tmpPath, sizeof(ProcManProcessTmpData)))
		goto error;

	// Attempt to open tmp file
	procManData.processes[pid].tmpFd=kernelFsFileOpen(tmpPath);
	if (procManData.processes[pid].tmpFd==KernelFsFdInvalid)
		goto error;

	// Initialise state
	procManData.processes[pid].state=ProcManProcessStateActive;

	// Initialise tmp file:
	ProcManProcessTmpData procTmpData;
	procTmpData.regs[ByteCodeRegisterIP]=0;
	procTmpData.skipFlag=false;
	procTmpData.envVars.stdioFd=KernelFsFdInvalid;

	strcpy(procTmpData.envVars.pwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(procTmpData.envVars.pwd, &dirname, &basename);
	assert(dirname==procTmpData.envVars.pwd);

	strcpy(procTmpData.envVars.path, "/bin");

	// TODO: Put programPath (or part of?) into argv[0]
	for(unsigned i=0; i<ARGVMAX; ++i)
		strcpy(procTmpData.envVars.argv[i], "");

	KernelFsFileOffset written=kernelFsFileWrite(procManData.processes[pid].tmpFd, (const uint8_t *)&procTmpData, sizeof(procTmpData));
	if (written<sizeof(procTmpData))
		goto error;

	return pid;

	error:
	if (pid!=ProcManPidMax) {
		kernelFsFileClose(procManData.processes[pid].progmemFd);
		procManData.processes[pid].progmemFd=KernelFsFdInvalid;
		kernelFsFileClose(procManData.processes[pid].tmpFd);
		procManData.processes[pid].tmpFd=KernelFsFdInvalid;
		kernelFsFileDelete(tmpPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
		procManData.processes[pid].state=ProcManProcessStateUnused;
	}

	return ProcManPidMax;
}

void procManProcessKill(ProcManPid pid) {
	// Close FDs
	KernelFsFd progmemFd=procManData.processes[pid].progmemFd;
	if (progmemFd!=KernelFsFdInvalid) {
		// progmemFd may be shared so check if anyone else is still using this one before closing it
		procManData.processes[pid].progmemFd=KernelFsFdInvalid;

		unsigned i;
		for(i=0; i<ProcManPidMax; ++i)
			if (procManData.processes[i].progmemFd==progmemFd)
				break;

		if (i==ProcManPidMax)
			kernelFsFileClose(progmemFd);
	}

	if (procManData.processes[pid].tmpFd!=KernelFsFdInvalid) {
		char tmpPath[KernelFsPathMax];
		strcpy(tmpPath, kernelFsGetFilePath(procManData.processes[pid].tmpFd));

		kernelFsFileClose(procManData.processes[pid].tmpFd);
		procManData.processes[pid].tmpFd=KernelFsFdInvalid;

		kernelFsFileDelete(tmpPath);
	}

	// Reset state
	procManData.processes[pid].state=ProcManProcessStateUnused;

	// Check if any processes are waiting due to waitpid syscall
	for(unsigned i=0; i<ProcManPidMax; ++i) {
		if (procManData.processes[i].state==ProcManProcessStateWaitingWaitpid && procManData.processes[i].waitingData==pid) {
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

	// Load tmp data
	ProcManProcessTmpData tmpData;
	bool res=procManProcessGetTmpData(process, &tmpData);
	assert(res);

	// Is this process not active?
	if (procManData.processes[pid].state!=ProcManProcessStateActive)
		return;

	// Grab instruction
	BytecodeInstructionLong instruction;
	if (!procManProcessMemoryReadByte(process, &tmpData, tmpData.regs[ByteCodeRegisterIP]++, &instruction[0]))
		goto kill;
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	if (length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)
		if (!procManProcessMemoryReadByte(process, &tmpData, tmpData.regs[ByteCodeRegisterIP]++, &instruction[1]))
			goto kill;
	if (length==BytecodeInstructionLengthLong)
		if (!procManProcessMemoryReadByte(process, &tmpData, tmpData.regs[ByteCodeRegisterIP]++, &instruction[2]))
			goto kill;

	// Are we meant to skip this instruction? (due to a previous skipN instruction)
	if (tmpData.skipFlag) {
		tmpData.skipFlag=false;
		goto done;
	}

	// Execute instruction
	if (!procManProcessExecInstruction(process, &tmpData, instruction))
		goto kill;

	// Save tmp data
	done:
	if (kernelFsFileWriteOffset(process->tmpFd, 0, (const uint8_t *)&tmpData, sizeof(ProcManProcessTmpData))!=sizeof(ProcManProcessTmpData)) {
		assert(false);
	}
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

ProcManPid procManGetPidFromProcess(ProcManProcess *process) {
	return process-procManData.processes;
}

const char *procManGetExecPathFromProcess(const ProcManProcess *process) {
	return kernelFsGetFilePath(process->progmemFd);
}

ProcManPid procManFindUnusedPid(void) {
	// We cannot use 0 as fork uses this to indicate success
	for(int i=1; i<ProcManPidMax; ++i)
		if (procManData.processes[i].state==ProcManProcessStateUnused)
			return i;
	return ProcManPidMax;
}

bool procManProcessGetTmpData(ProcManProcess *process, ProcManProcessTmpData *tmpData) {
	return(kernelFsFileReadOffset(process->tmpFd, 0, (uint8_t *)tmpData, sizeof(ProcManProcessTmpData))==sizeof(ProcManProcessTmpData));
}

bool procManProcessMemoryReadByte(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, uint8_t *value) {
	if (addr<ByteCodeMemoryRamAddr) {
		// Addresss is in progmem data
		if (kernelFsFileReadOffset(process->progmemFd, addr, value, 1)==1)
			return true;
		else {
			debugLog("warning: process %u (%s) tried to read invalid address (0x%04X, pointing to PROGMEM at offset %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, addr);
			return false;
		}
	} else {
		// Address is in RAM
		ByteCodeWord ramIndex=(addr-ByteCodeMemoryRamAddr);
		if (ramIndex<ProcManProcessRamSize) {
			*value=tmpData->ram[ramIndex];
			return true;
		} else {
			debugLog("warning: process %u (%s) tried to read invalid address (0x%04X, pointing to RAM at offset %u, but size is only %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, ProcManProcessRamSize);
			return false;
		}
	}
}

bool procManProcessMemoryReadWord(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, ByteCodeWord *value) {
	uint8_t upper, lower;
	if (!procManProcessMemoryReadByte(process, tmpData, addr, &upper))
		return false;
	if (!procManProcessMemoryReadByte(process, tmpData, addr+1, &lower))
		return false;
	*value=(((ByteCodeWord)upper)<<8)|((ByteCodeWord)lower);
	return true;
}

bool procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, char *str, uint16_t len) {
	while(len-->0) {
		uint8_t c;
		if (!procManProcessMemoryReadByte(process, tmpData, addr++, &c))
			return false;
		*str++=c;
		if (c=='\0')
			break;
	}

	return true;
}

bool procManProcessMemoryWriteByte(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, uint8_t value) {
	// Is this addr in read-only progmem section?
	if (addr<ByteCodeMemoryRamAddr) {
		debugLog("warning: process %u (%s) tried to write to read only address (0x%04X), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr);
		return false;
	}

	// addr is in RAM
	ByteCodeWord ramIndex=(addr-ByteCodeMemoryRamAddr);
	if (ramIndex<ProcManProcessRamSize) {
		tmpData->ram[ramIndex]=value;
		return true;
	} else {
			debugLog("warning: process %u (%s) tried to write invalid address (0x%04X, pointing to RAM at offset %u, but size is only %u), killing\n", procManGetPidFromProcess(process), procManGetExecPathFromProcess(process), addr, ramIndex, ProcManProcessRamSize);
		return false;
	}
}

bool procManProcessMemoryWriteWord(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, ByteCodeWord value) {
	if (!procManProcessMemoryWriteByte(process, tmpData, addr, (value>>8)))
		return false;
	if (!procManProcessMemoryWriteByte(process, tmpData, addr+1, (value&0xFF)))
		return false;
	return true;
}

bool procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, const char *str) {
	for(const char *c=str; ; ++c) {
		if (!procManProcessMemoryWriteByte(process, tmpData, addr++, *c))
			return false;
		if (*c=='\0')
			break;
	}
	return true;
}

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessTmpData *tmpData, BytecodeInstructionLong instruction) {
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
					if (!procManProcessMemoryWriteByte(process, tmpData, tmpData->regs[info.d.memory.destReg], tmpData->regs[info.d.memory.srcReg]))
						return false;
				break;
				case BytecodeInstructionMemoryTypeLoad8: {
					uint8_t value;
					if (!procManProcessMemoryReadByte(process, tmpData, tmpData->regs[info.d.memory.srcReg], &value))
						return false;
					tmpData->regs[info.d.memory.destReg]=value;
				} break;
				case BytecodeInstructionMemoryTypeReserved:
					return false;
				break;
			}
		break;
		case BytecodeInstructionTypeAlu: {
			int opA=tmpData->regs[info.d.alu.opAReg];
			int opB=tmpData->regs[info.d.alu.opBReg];
			switch(info.d.alu.type) {
				case BytecodeInstructionAluTypeInc:
					tmpData->regs[info.d.alu.destReg]+=info.d.alu.incDecValue;
				break;
				case BytecodeInstructionAluTypeDec:
					tmpData->regs[info.d.alu.destReg]-=info.d.alu.incDecValue;
				break;
				case BytecodeInstructionAluTypeAdd:
					tmpData->regs[info.d.alu.destReg]=opA+opB;
				break;
				case BytecodeInstructionAluTypeSub:
					tmpData->regs[info.d.alu.destReg]=opA-opB;
				break;
				case BytecodeInstructionAluTypeMul:
					tmpData->regs[info.d.alu.destReg]=opA*opB;
				break;
				case BytecodeInstructionAluTypeDiv:
					tmpData->regs[info.d.alu.destReg]=opA/opB;
				break;
				case BytecodeInstructionAluTypeXor:
					tmpData->regs[info.d.alu.destReg]=opA^opB;
				break;
				case BytecodeInstructionAluTypeOr:
					tmpData->regs[info.d.alu.destReg]=opA|opB;
				break;
				case BytecodeInstructionAluTypeAnd:
					tmpData->regs[info.d.alu.destReg]=opA&opB;
				break;
				case BytecodeInstructionAluTypeNot:
					tmpData->regs[info.d.alu.destReg]=~opA;
				break;
				case BytecodeInstructionAluTypeCmp: {
					ByteCodeWord *d=&tmpData->regs[info.d.alu.destReg];
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
					tmpData->regs[info.d.alu.destReg]=opA<<opB;
				break;
				case BytecodeInstructionAluTypeShiftRight:
					tmpData->regs[info.d.alu.destReg]=opA>>opB;
				break;
				case BytecodeInstructionAluTypeSkip:
					tmpData->skipFlag=(tmpData->regs[info.d.alu.destReg] & (1u<<info.d.alu.opAReg));
				break;
				case BytecodeInstructionAluTypeStore16: {
					ByteCodeWord destAddr=tmpData->regs[info.d.alu.destReg];
					if (!procManProcessMemoryWriteWord(process, tmpData, destAddr, opA))
						return false;
				} break;
				case BytecodeInstructionAluTypeLoad16: {
					ByteCodeWord srcAddr=tmpData->regs[info.d.alu.opAReg];
					if (!procManProcessMemoryReadWord(process, tmpData, srcAddr, &tmpData->regs[info.d.alu.destReg]))
						return false;
				} break;
			}
		} break;
		case BytecodeInstructionTypeMisc:
			switch(info.d.misc.type) {
				case BytecodeInstructionMiscTypeNop:
				break;
				case BytecodeInstructionMiscTypeSyscall: {
					uint16_t syscallId=tmpData->regs[0];
					switch(syscallId) {
						case ByteCodeSyscallIdExit:
							return false; // TODO: pass on exit status
						break;
						case ByteCodeSyscallIdGetPid:
							tmpData->regs[0]=procManGetPidFromProcess(process);
						break;
						case ByteCodeSyscallIdGetArgC: {
							tmpData->regs[0]=0;
							for(unsigned i=0; i<ARGVMAX; ++i)
								tmpData->regs[0]+=(strlen(tmpData->envVars.argv[i])>0);
						} break;
						case ByteCodeSyscallIdGetArgVN: {
							int n=tmpData->regs[1];
							ByteCodeWord bufAddr=tmpData->regs[2];
							// TODO: Use this: ByteCodeWord bufLen=tmpData->regs[3];

							if (n>ARGVMAX)
								tmpData->regs[0]=0;
							else {
								if (!procManProcessMemoryWriteStr(process, tmpData, bufAddr, tmpData->envVars.argv[n]))
									return false;
								tmpData->regs[0]=strlen(tmpData->envVars.argv[n]);
							}
						} break;
						case ByteCodeSyscallIdFork:
							procManProcessFork(process, tmpData);
						break;
						case ByteCodeSyscallIdExec:
							if (!procManProcessExec(process, tmpData))
								return false;
						break;
						case ByteCodeSyscallIdWaitPid: {
							ByteCodeWord waitPid=tmpData->regs[1];

							// If given pid does not represent a process, return immediately
							if (procManGetProcessByPid(waitPid)!=NULL) {
								// Otherwise indicate process is waiting for this pid to die
								process->state=ProcManProcessStateWaitingWaitpid;
								process->waitingData=waitPid;
							}
						} break;
						case ByteCodeSyscallIdRead: {
							KernelFsFd fd=tmpData->regs[1];
							uint16_t offset=tmpData->regs[2];
							uint16_t bufAddr=tmpData->regs[3];
							KernelFsFileOffset len=tmpData->regs[4];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value;
								if (kernelFsFileReadOffset(fd, offset+i, &value, 1)!=1)
									break;
								if (!procManProcessMemoryWriteByte(process, tmpData, bufAddr+i, value))
									return false;
							}
							tmpData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdWrite: {
							KernelFsFd fd=tmpData->regs[1];
							uint16_t offset=tmpData->regs[2];
							uint16_t bufAddr=tmpData->regs[3];
							KernelFsFileOffset len=tmpData->regs[4];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value;
								if (!procManProcessMemoryReadByte(process, tmpData, bufAddr+i, &value))
									return false;
								if (kernelFsFileWriteOffset(fd, offset+i, &value, 1)!=1)
									break;
							}
							tmpData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdOpen: {
							char path[KernelFsPathMax];
							if (!procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], path, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(path);
							tmpData->regs[0]=kernelFsFileOpen(path);
						} break;
						case ByteCodeSyscallIdClose:
							kernelFsFileClose(tmpData->regs[1]);
						break;
						case ByteCodeSyscallIdDirGetChildN: {
							KernelFsFd fd=tmpData->regs[1];
							ByteCodeWord childNum=tmpData->regs[2];
							uint16_t bufAddr=tmpData->regs[3];

							char childPath[KernelFsPathMax];
							bool result=kernelFsDirectoryGetChild(fd, childNum, childPath);

							if (result) {
								if (!procManProcessMemoryWriteStr(process, tmpData, bufAddr, childPath))
									return false;
								tmpData->regs[0]=1;
							} else {
								tmpData->regs[0]=0;
							}
						} break;
						case ByteCodeSyscallIdGetPath: {
							KernelFsFd fd=tmpData->regs[1];
							uint16_t bufAddr=tmpData->regs[2];

							const char *srcPath=kernelFsGetFilePath(fd);
							if (srcPath==NULL)
								tmpData->regs[0]=0;
							else {
								if (!procManProcessMemoryWriteStr(process, tmpData, bufAddr, srcPath))
									return false;
								tmpData->regs[0]=1;
							}
						} break;
						case ByteCodeSyscallIdEnvGetStdioFd:
							tmpData->regs[0]=tmpData->envVars.stdioFd;
						break;
						case ByteCodeSyscallIdEnvSetStdioFd:
							tmpData->envVars.stdioFd=tmpData->regs[1];
						break;
						case ByteCodeSyscallIdEnvGetPwd:
							if (!procManProcessMemoryWriteStr(process, tmpData, tmpData->regs[1], tmpData->envVars.pwd))
								return false;
						break;
						case ByteCodeSyscallIdEnvSetPwd:
							if (!procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], tmpData->envVars.pwd, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(tmpData->envVars.pwd);
						break;
						case ByteCodeSyscallIdEnvGetPath:
							if (!procManProcessMemoryWriteStr(process, tmpData, tmpData->regs[1], tmpData->envVars.path))
								return false;
						break;
						case ByteCodeSyscallIdEnvSetPath:
							if (!procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], tmpData->envVars.path, KernelFsPathMax))
								return false;
							kernelFsPathNormalise(tmpData->envVars.path);
						break;
						case ByteCodeSyscallIdTimeMonotonic:
							tmpData->regs[0]=(millis()/1000);
						break;
						default:
							debugLog("warning: invalid syscall id=%i, process %u (%s), killing\n", syscallId, procManGetPidFromProcess(process), procManGetExecPathFromProcess(process));
							return false;
						break;
					}
				} break;
				case BytecodeInstructionMiscTypeSet8:
					tmpData->regs[info.d.misc.d.set8.destReg]=info.d.misc.d.set8.value;
				break;
				case BytecodeInstructionMiscTypeSet16:
					tmpData->regs[info.d.misc.d.set16.destReg]=info.d.misc.d.set16.value;
				break;
			}
		break;
	}

	return true;
}

void procManProcessFork(ProcManProcess *process, ProcManProcessTmpData *tmpData) {
	ProcManPid parentPid=procManGetPidFromProcess(process);

	// Find a PID for the new process
	ProcManPid childPid=procManFindUnusedPid();
	if (childPid==ProcManPidMax)
		goto error;

	// Construct tmp file path
	// TODO: Try others if exists
	char childTmpPath[KernelFsPathMax];
	sprintf(childTmpPath, "/tmp/proc%u", childPid);

	// Attempt to create tmp file
	if (!kernelFsFileCreateWithSize(childTmpPath, sizeof(ProcManProcessTmpData)))
		goto error;

	// Attempt to open tmp file
	procManData.processes[childPid].tmpFd=kernelFsFileOpen(childTmpPath);
	if (procManData.processes[childPid].tmpFd==KernelFsFdInvalid)
		goto error;

	// Initialise tmp file:
	ProcManProcessTmpData childTmpData=*tmpData;
	childTmpData.regs[0]=0; // indicate success in the child

	KernelFsFileOffset written=kernelFsFileWrite(procManData.processes[childPid].tmpFd, (const uint8_t *)&childTmpData, sizeof(childTmpData));
	if (written<sizeof(childTmpData))
		goto error;

	// Simply use same FD as parent for the program data
	procManData.processes[childPid].state=ProcManProcessStateActive;
	procManData.processes[childPid].progmemFd=procManData.processes[parentPid].progmemFd;

	// Update parent return value with child's PID
	tmpData->regs[0]=childPid;

	return;

	error:
	if (childPid!=ProcManPidMax) {
		procManData.processes[childPid].progmemFd=KernelFsFdInvalid;
		kernelFsFileClose(procManData.processes[childPid].tmpFd);
		procManData.processes[childPid].tmpFd=KernelFsFdInvalid;
		kernelFsFileDelete(childTmpPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
		procManData.processes[childPid].state=ProcManProcessStateUnused;
	}

	// Indicate error
	tmpData->regs[0]=ProcManPidMax;
}

bool procManProcessExec(ProcManProcess *process, ProcManProcessTmpData *tmpData) {
	// Grab path and args (if any)
	char args[ARGVMAX][64]; // TODO: Avoid hardcoded 64
	for(unsigned i=0; i<ARGVMAX; ++i)
		args[i][0]='\0';

	if (!procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], args[0], KernelFsPathMax))
		return false;

	for(unsigned i=1; i<ARGVMAX; ++i)
		if (tmpData->regs[i+1]!=0)
			if (!procManProcessMemoryReadStr(process, tmpData, tmpData->regs[i+1], args[i], KernelFsPathMax))
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
	tmpData->regs[ByteCodeRegisterIP]=0;

	// Update argv array
	for(unsigned i=0; i<ARGVMAX; ++i)
		strcpy(tmpData->envVars.argv[i], args[i]);

	return true;
}
