#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "kernelfs.h"
#include "procman.h"

#define ProcManProcessRamSize 512 // TODO: Allow this to be dynamic

typedef enum {
	ProcManProcessStateActive,
	ProcManProcessStateWaiting, // waiting on return of a syscall such as read
} ProcManProcessState;

typedef struct {
	KernelFsFd stdioFd; // set to KernelFsInvalid when init is called
	char pwd[KernelFsPathMax]; // set to '/' when init is called
} ProcManProcessEnvVars;

typedef struct {
	ByteCodeWord regs[BytecodeRegisterNB];
	ProcManProcessEnvVars envVars;
	ProcManProcessState state;
	bool skipFlag; // skip next instruction?
	uint8_t ram[ProcManProcessRamSize];
} ProcManProcessTmpData;

typedef struct {
	KernelFsFd progmemFd, tmpFd;
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

ProcManPid procManFindUnusedPid(void);

bool procManProcessGetTmpData(ProcManProcess *process, ProcManProcessTmpData *tmpData);
uint8_t procManProcessMemoryRead(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr);
void procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, char *str, uint16_t len);
void procManProcessMemoryWrite(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, uint8_t value);
void procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, const char *str);

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessTmpData *tmpData, BytecodeInstructionLong instruction);

void procManProcessFork(ProcManProcess *process, ProcManProcessTmpData *tmpData);
void procManProcessExec(ProcManProcess *process, ProcManProcessTmpData *tmpData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void) {
	// Clear fds
	for(int i=0; i<ProcManPidMax; ++i) {
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

	// Initialise tmp file:
	ProcManProcessTmpData procTmpData;
	procTmpData.regs[ByteCodeRegisterIP]=0;
	procTmpData.skipFlag=false;
	procTmpData.state=ProcManProcessStateActive;
	procTmpData.envVars.stdioFd=KernelFsFdInvalid;

	strcpy(procTmpData.envVars.pwd, programPath);
	char *dirname, *basename;
	kernelFsPathSplit(procTmpData.envVars.pwd, &dirname, &basename);
	assert(dirname==procTmpData.envVars.pwd);

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

	// Grab instruction
	ByteCodeWord originalIP=tmpData.regs[ByteCodeRegisterIP];

	BytecodeInstructionLong instruction;
	instruction[0]=procManProcessMemoryRead(process, &tmpData, tmpData.regs[ByteCodeRegisterIP]++);
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	if (length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)
		instruction[1]=procManProcessMemoryRead(process, &tmpData, tmpData.regs[ByteCodeRegisterIP]++);
	if (length==BytecodeInstructionLengthLong)
		instruction[2]=procManProcessMemoryRead(process, &tmpData, tmpData.regs[ByteCodeRegisterIP]++);

	// Are we meant to skip this instruction? (due to a previous skipN instruction)
	if (tmpData.skipFlag) {
		tmpData.skipFlag=false;
		goto done;
	}

	// Execute instruction
	if (!procManProcessExecInstruction(process, &tmpData, instruction)) {
		procManProcessKill(pid);
		return; // return to avoid saving data
	}

	// If process is in state waiting, revert to last instruction again
	if (tmpData.state==ProcManProcessStateWaiting)
		tmpData.regs[ByteCodeRegisterIP]=originalIP;

	// Save tmp data
	done:
	if (kernelFsFileWriteOffset(process->tmpFd, 0, (const uint8_t *)&tmpData, sizeof(ProcManProcessTmpData))!=sizeof(ProcManProcessTmpData)) {
		assert(false);
	}
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

ProcManPid procManFindUnusedPid(void) {
	// We cannot use 0 as fork uses this to indicate success
	for(int i=1; i<ProcManPidMax; ++i)
		if (procManData.processes[i].progmemFd==KernelFsFdInvalid)
			return i;
	return ProcManPidMax;
}

bool procManProcessGetTmpData(ProcManProcess *process, ProcManProcessTmpData *tmpData) {
	return(kernelFsFileReadOffset(process->tmpFd, 0, (uint8_t *)tmpData, sizeof(ProcManProcessTmpData))==sizeof(ProcManProcessTmpData));
}

uint8_t procManProcessMemoryRead(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr) {
	if (addr<ByteCodeMemoryRamAddr) {
		uint8_t value=0xFF;
		bool res=kernelFsFileReadOffset(process->progmemFd, addr, &value, 1);
		assert(res==1);
		return value;
	} else
		return tmpData->ram[addr-ByteCodeMemoryRamAddr];
}

void procManProcessMemoryReadStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, char *str, uint16_t len) {
	while(len-->0) {
		uint8_t c=procManProcessMemoryRead(process, tmpData, addr++);
		*str++=c;
		if (c=='\0')
			break;
	}
}

void procManProcessMemoryWrite(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, uint8_t value) {
	if (addr<ByteCodeMemoryRamAddr)
		assert(false); // read-only
	else
		tmpData->ram[addr-ByteCodeMemoryRamAddr]=value;
}

void procManProcessMemoryWriteStr(ProcManProcess *process, ProcManProcessTmpData *tmpData, ByteCodeWord addr, const char *str) {
	for(const char *c=str; ; ++c) {
		procManProcessMemoryWrite(process, tmpData, addr++, *c);
		if (*c=='\0')
			break;
	}
}

bool procManProcessExecInstruction(ProcManProcess *process, ProcManProcessTmpData *tmpData, BytecodeInstructionLong instruction) {
	// Parse instruction
	BytecodeInstructionInfo info;
	if (!bytecodeInstructionParse(&info, instruction))
		return false;

	// Execute instruction
	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore:
					procManProcessMemoryWrite(process, tmpData, tmpData->regs[info.d.memory.destReg], tmpData->regs[info.d.memory.srcReg]);
				break;
				case BytecodeInstructionMemoryTypeLoad:
					tmpData->regs[info.d.memory.destReg]=procManProcessMemoryRead(process, tmpData, tmpData->regs[info.d.memory.srcReg]);
				break;
				case BytecodeInstructionMemoryTypeReserved:
					return false;
				break;
			}
		break;
		case BytecodeInstructionTypeAlu: {
			int opA=tmpData->regs[info.d.alu.opAReg];
			int opB=tmpData->regs[info.d.alu.opBReg];
			switch(info.d.alu.type) {
				case BytecodeInstructionAluTypeInc: {
					tmpData->regs[info.d.alu.destReg]+=info.d.alu.incDecValue;
				} break;
				case BytecodeInstructionAluTypeDec: {
					tmpData->regs[info.d.alu.destReg]-=info.d.alu.incDecValue;
				} break;
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
					procManProcessMemoryWrite(process, tmpData, destAddr, (opA>>8));
					procManProcessMemoryWrite(process, tmpData, destAddr+1, (opA&0xFF));
				} break;
				case BytecodeInstructionAluTypeLoad16: {
					ByteCodeWord srcAddr=tmpData->regs[info.d.alu.opAReg];
					tmpData->regs[info.d.alu.destReg]=procManProcessMemoryRead(process, tmpData, srcAddr);
					tmpData->regs[info.d.alu.destReg]<<=8;
					tmpData->regs[info.d.alu.destReg]|=procManProcessMemoryRead(process, tmpData, srcAddr+1);
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
						case ByteCodeSyscallIdGetArgC:
							tmpData->regs[0]=0; // TODO: this
						break;
						case ByteCodeSyscallIdGetArgVN:
							tmpData->regs[0]=0; // TODO: this
						break;
						case ByteCodeSyscallIdFork:
							procManProcessFork(process, tmpData);
						break;
						case ByteCodeSyscallIdExec:
							procManProcessExec(process, tmpData);
						break;
						case ByteCodeSyscallIdRead: {
							KernelFsFd fd=tmpData->regs[1];
							uint16_t bufAddr=tmpData->regs[2];
							KernelFsFileOffset len=tmpData->regs[3];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value;
								if (kernelFsFileReadOffset(fd, i, &value, 1)!=1)
									break;
								procManProcessMemoryWrite(process, tmpData, bufAddr+i, value);
							}
							tmpData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdWaitPid: {
							ByteCodeWord waitPid=tmpData->regs[1];

							// Check if given pid does not represent a process (indicating it has been killed)
							if (procManGetProcessByPid(waitPid)==NULL)
								// We can resume process (if it was even paused - we may simply succeed immediately)
								tmpData->state=ProcManProcessStateActive;
							else
								// Set process as Waiting so we retry next tick
								tmpData->state=ProcManProcessStateWaiting;
						} break;
						case ByteCodeSyscallIdWrite: {
							KernelFsFd fd=tmpData->regs[1];
							uint16_t bufAddr=tmpData->regs[2];
							KernelFsFileOffset len=tmpData->regs[3];

							KernelFsFileOffset i;
							for(i=0; i<len; ++i) {
								uint8_t value=procManProcessMemoryRead(process, tmpData, bufAddr+i);
								if (kernelFsFileWriteOffset(fd, i, &value, 1)!=1)
									break;
							}
							tmpData->regs[0]=i;
						} break;
						case ByteCodeSyscallIdOpen: {
							char path[KernelFsPathMax];
							procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], path, KernelFsPathMax);
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
								procManProcessMemoryWriteStr(process, tmpData, bufAddr, childPath);
								tmpData->regs[0]=1;
							} else {
								tmpData->regs[0]=0;
							}
						} break;
						case ByteCodeSyscallIdEnvGetStdioFd:
							tmpData->regs[0]=tmpData->envVars.stdioFd;
						break;
						case ByteCodeSyscallIdEnvSetStdioFd:
							tmpData->envVars.stdioFd=tmpData->regs[1];
						break;
						case ByteCodeSyscallIdEnvGetPwd:
							procManProcessMemoryWriteStr(process, tmpData, tmpData->regs[1], tmpData->envVars.pwd);
						break;
						case ByteCodeSyscallIdEnvSetPwd:
							procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], tmpData->envVars.pwd, KernelFsPathMax);
							kernelFsPathNormalise(tmpData->envVars.pwd);
						break;
						default:
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
	}

	// Indicate error
	tmpData->regs[0]=ProcManPidMax;
}

void procManProcessExec(ProcManProcess *process, ProcManProcessTmpData *tmpData) {
	// Grab path
	char execPath[KernelFsPathMax];
	procManProcessMemoryReadStr(process, tmpData, tmpData->regs[1], execPath, KernelFsPathMax);

	// Normalise path and then check if valid
	kernelFsPathNormalise(execPath);

	if (!kernelFsPathIsValid(execPath))
		return;

	// Attempt to open new program file
	KernelFsFd newProgmemFd=kernelFsFileOpen(execPath);
	if (newProgmemFd==KernelFsFdInvalid)
		return;

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

	return;
}
