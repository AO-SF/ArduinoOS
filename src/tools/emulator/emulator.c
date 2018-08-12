#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bytecode.h"
#include "procman.h"

#define PathMax 64 // TODO: use KernelFsPathMax
#define InitPid 0
#define StdioFd 1

typedef struct {
	int argc;
	char **argv;

	char pwd[PathMax];
	char path[PathMax];
	uint8_t stdioFd;
} ProcessEnvVars;

typedef struct {
	ByteCodeWord regs[8];

	uint8_t memory[ByteCodeMemoryTotalSize]; // combined progmem+ram

	bool skipFlag; // skip next instruction?

	unsigned instructionCount;
	ByteCodeWord pid;

	ProcessEnvVars envVars;
} Process;

Process *process=NULL;
bool infoSyscalls=false;
bool infoInstructions=false;
bool infoState=false;
bool slow=false;
bool passOnExitStatus=false;
int exitStatus=EXIT_SUCCESS;

bool processRunNextInstruction(Process *process);
void processDebug(const Process *process);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc<2) {
		printf("Usage: %s [--infosyscalls] [--infoinstructions] [--infostate] [--slow] [--passonexitstatus] inputfile [inputargs ...]\n", argv[0]);
		goto done;
	}

	int i;
	for(i=1; i<argc; ++i) {
		if (strncmp(argv[i], "--", 2)!=0)
			break;
		if (strcmp(argv[i], "--infoSyscalls")==0)
			infoSyscalls=true;
		else if (strcmp(argv[i], "--infoInstructions")==0)
			infoInstructions=true;
		else if (strcmp(argv[i], "--infoState")==0)
			infoState=true;
		else if (strcmp(argv[i], "--passonexitstatus")==0)
			passOnExitStatus=true;
		else if (strcmp(argv[i], "--slow")==0)
			slow=true;
		else
			printf("Warning: unknown option '%s'\n", argv[i]);
	}

	const int inputArgBaseIndex=i;

	// Allocate process data struct
	process=malloc(sizeof(Process));
	if (process==NULL) {
		printf("Error: Could not allocate process data struct\n");
		goto done;
	}

	process->regs[ByteCodeRegisterSP]=0;
	process->regs[ByteCodeRegisterIP]=0;
	process->skipFlag=false;
	process->instructionCount=0;
	srand(time(NULL));
	process->pid=(rand()%(ProcManPidMax-1))+1; // +1 to avoid InitPid at 0

	process->envVars.argc=argc-inputArgBaseIndex;
	process->envVars.argv=argv+inputArgBaseIndex;
	strcpy(process->envVars.pwd, "/bin");
	strcpy(process->envVars.path, "/bin");
	process->envVars.stdioFd=StdioFd;

	// Read-in input file
	inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Error: Could not open input file '%s' for reading\n", inputPath);
		goto done;
	}

	int c;
	uint8_t *next=process->memory;
	while((c=fgetc(inputFile))!=EOF)
		*next++=c;

	// Run process
	do {
		if (slow)
			sleep(1);

		if (infoState)
			processDebug(process);
	} while(processRunNextInstruction(process));

	// Done
	done:
	if (inputFile!=NULL)
		fclose(inputFile);
	free(process);

	return exitStatus;
}

bool processRunNextInstruction(Process *process) {
	BytecodeInstructionLong instruction;
	instruction[0]=process->memory[process->regs[ByteCodeRegisterIP]++];
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	if (length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)
		instruction[1]=process->memory[process->regs[ByteCodeRegisterIP]++];
	if (length==BytecodeInstructionLengthLong)
		instruction[2]=process->memory[process->regs[ByteCodeRegisterIP]++];

	BytecodeInstructionInfo info;
	if (!bytecodeInstructionParse(&info, instruction)) {
		if (infoInstructions)
			printf("Error: Invalid instruction (bad bit sequence)\n");
		return false;
	}

	if (process->skipFlag) {
		if (infoInstructions)
			printf("Info: Skipping instruction\n");
		process->skipFlag=false;
		return true;
	}

	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore8:
					process->memory[process->regs[info.d.memory.destReg]]=process->regs[info.d.memory.srcReg];
					if (infoInstructions)
						printf("Info: *r%i=r%i (*%u=%u)\n", info.d.memory.destReg, info.d.memory.srcReg, process->regs[info.d.memory.destReg], process->regs[info.d.memory.srcReg]);
				break;
				case BytecodeInstructionMemoryTypeLoad8: {
					ByteCodeWord srcAddr=process->regs[info.d.memory.srcReg];
					process->regs[info.d.memory.destReg]=process->memory[srcAddr];
					if (infoInstructions)
						printf("Info: r%i=*r%i (=*%i=%i)\n", info.d.memory.destReg, info.d.memory.srcReg, srcAddr, process->regs[info.d.memory.destReg]);
				} break;
				case BytecodeInstructionMemoryTypeReserved:
					printf("Error: Invalid instruction (reserved bit sequence)\n");
					return false;
				break;
			}
		break;
		case BytecodeInstructionTypeAlu: {
			int opA=process->regs[info.d.alu.opAReg];
			int opB=process->regs[info.d.alu.opBReg];
			switch(info.d.alu.type) {
				case BytecodeInstructionAluTypeInc: {
					int pre=process->regs[info.d.alu.destReg]+=info.d.alu.incDecValue;
					if (infoInstructions) {
						if (info.d.alu.incDecValue==1)
							printf("Info: r%i++ (r%i=%i+1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
						else
							printf("Info: r%i+=%i (r%i=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.incDecValue, info.d.alu.destReg, pre, info.d.alu.incDecValue, process->regs[info.d.alu.destReg]);
					}
				} break;
				case BytecodeInstructionAluTypeDec: {
					int pre=process->regs[info.d.alu.destReg]-=info.d.alu.incDecValue;
					if (infoInstructions) {
						if (info.d.alu.incDecValue==1)
							printf("Info: r%i-- (r%i=%i-1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
						else
							printf("Info: r%i-=%i (r%i=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.incDecValue, info.d.alu.destReg, pre, info.d.alu.incDecValue, process->regs[info.d.alu.destReg]);
					}
				} break;
				case BytecodeInstructionAluTypeAdd:
					process->regs[info.d.alu.destReg]=opA+opB;
					if (infoInstructions)
						printf("Info: r%i=r%i+r%i (=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSub:
					process->regs[info.d.alu.destReg]=opA-opB;
					if (infoInstructions)
						printf("Info: r%i=r%i-r%i (=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeMul:
					process->regs[info.d.alu.destReg]=opA*opB;
					if (infoInstructions)
						printf("Info: r%i=r%i*r%i (=%i*%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeDiv:
					process->regs[info.d.alu.destReg]=opA/opB;
					if (infoInstructions)
						printf("Info: r%i=r%i/r%i (=%i/%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeXor:
					process->regs[info.d.alu.destReg]=opA^opB;
					if (infoInstructions)
						printf("Info: r%i=r%i^r%i (=%i^%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeOr:
					process->regs[info.d.alu.destReg]=opA|opB;
					if (infoInstructions)
						printf("Info: r%i=r%i|r%i (=%i|%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeAnd:
					process->regs[info.d.alu.destReg]=opA&opB;
					if (infoInstructions)
						printf("Info: r%i=r%i&r%i (=%i&%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeNot:
					process->regs[info.d.alu.destReg]=~opA;
					if (infoInstructions)
						printf("Info: r%i=~r%i (=~%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, opA, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeCmp: {
					ByteCodeWord *d=&process->regs[info.d.alu.destReg];
					*d=0;
					*d|=(opA==opB)<<BytecodeInstructionAluCmpBitEqual;
					*d|=(opA==0)<<BytecodeInstructionAluCmpBitEqualZero;
					*d|=(opA!=opB)<<BytecodeInstructionAluCmpBitNotEqual;
					*d|=(opA!=0)<<BytecodeInstructionAluCmpBitNotEqualZero;
					*d|=(opA<opB)<<BytecodeInstructionAluCmpBitLessThan;
					*d|=(opA<=opB)<<BytecodeInstructionAluCmpBitLessEqual;
					*d|=(opA>opB)<<BytecodeInstructionAluCmpBitGreaterThan;
					*d|=(opA>=opB)<<BytecodeInstructionAluCmpBitGreaterEqual;

					if (infoInstructions)
						printf("Info: r%i=cmp(r%i,r%i) (=cmp(%i,%i)=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				} break;
				case BytecodeInstructionAluTypeShiftLeft:
					process->regs[info.d.alu.destReg]=opA<<opB;
					if (infoInstructions)
						printf("Info: r%i=r%i<<r%i (=%i<<%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeShiftRight:
					process->regs[info.d.alu.destReg]=opA>>opB;
					if (infoInstructions)
						printf("Info: r%i=r%i>>r%i (=%i>>%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSkip:
					process->skipFlag=(process->regs[info.d.alu.destReg] & (1u<<info.d.alu.opAReg));

					if (infoInstructions)
						printf("Info: skip%u r%i (=%i, %s, skip=%i)\n", info.d.alu.opAReg, info.d.alu.destReg, process->regs[info.d.alu.destReg], byteCodeInstructionAluCmpBitStrings[info.d.alu.opAReg], process->skipFlag);
				break;
				case BytecodeInstructionAluTypeStore16:
					process->memory[process->regs[info.d.alu.destReg]]=(process->regs[info.d.alu.opAReg]>>8);
					process->memory[process->regs[info.d.alu.destReg]+1]=(process->regs[info.d.alu.opAReg]&0xFF);
					if (infoInstructions)
						printf("Info: [r%i]=r%i (16 bit) ([%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, process->regs[info.d.alu.destReg], opA);
				break;
				case BytecodeInstructionAluTypeLoad16:
					process->regs[info.d.alu.destReg]=(((ByteCodeWord)process->memory[process->regs[info.d.alu.opAReg]])<<8) |
					                                   process->memory[process->regs[info.d.alu.opAReg]+1];
					if (infoInstructions)
						printf("Info: r%i=[r%i] (16 bit) (=[%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, opA, process->regs[info.d.alu.destReg]);
				break;
			}
		} break;
		case BytecodeInstructionTypeMisc:
			switch(info.d.misc.type) {
				case BytecodeInstructionMiscTypeNop:
					if (infoInstructions)
						printf("Info: nop\n");
				break;
				case BytecodeInstructionMiscTypeSyscall: {
					uint16_t syscallId=process->regs[0];
					if (infoInstructions)
						printf("Info: syscall id=%i\n", syscallId);
					switch(syscallId) {
						case ByteCodeSyscallIdExit:
							if (passOnExitStatus)
								exitStatus=process->regs[1];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [exit], status=%u)\n", syscallId, process->regs[1]);
							return false;
						break;
						case ByteCodeSyscallIdGetPid: {
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpid], return=%u)\n", syscallId, process->pid);

							process->regs[0]=process->pid;
						} break;
						case ByteCodeSyscallIdGetArgC: {
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getargc], return=%u)\n", syscallId, process->envVars.argc);

							process->regs[0]=process->envVars.argc;
						} break;
						case ByteCodeSyscallIdGetArgVN: {
							int n=process->regs[1];
							int buf=process->regs[2];
							int len=process->regs[3];

							if (n>=process->envVars.argc) {
								process->regs[0]=0;

								if (infoSyscalls)
									printf("Info: syscall(id=%i [getargvn], n=%i, buf=%i, len=%i, return=0 as n>=argc)\n", syscallId, n, buf, len);
							} else {
								const char *str=process->envVars.argv[n];
								int trueLen=strlen(str);

								if (infoSyscalls)
									printf("Info: syscall(id=%i [getargvn], n=%i, buf=%i, len=%i, return=%u with '%s')\n", syscallId, n, buf, len, trueLen, str);

								for(int i=0; i<len && i<trueLen; ++i)
									process->memory[buf+i]=str[i];
								process->regs[0]=trueLen;
							}
						} break;
						case ByteCodeSyscallIdFork:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [fork] (unimplemented)\n", syscallId);

							// This is not implemented - simply return error
							process->regs[0]=ProcManPidMax;
						break;
						case ByteCodeSyscallIdExec:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [exec] (unimplemented)\n", syscallId);

							// This is not implemented - simply return false
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdWaitPid: {
							ProcManPid waitPid=process->regs[1];
							ByteCodeWord timeout=process->regs[2];

							if (infoSyscalls)
								printf("Info: syscall(id=%i [waitpid], pid=%u, timeout=%u\n", syscallId, waitPid, timeout);
							if (waitPid==InitPid || waitPid==process->pid) {
								if (timeout==0) {
									printf("Warning: Entered infinite waitpid syscall (waiting for own or init's PID with infinite timeout), exiting\n");
									return false;
								}
								sleep(timeout);
								process->regs[0]=ProcManExitStatusTimeout;
							} else {
								process->regs[0]=ProcManExitStatusNoProcess;
							}
						} break;
						case ByteCodeSyscallIdGetPidPath:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidpath] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdGetPidState:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidstate] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdGetAllCpuCounts:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getallcpucounts] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdKill: {
							ProcManPid pid=process->regs[1];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [kill], pid=%u\n", syscallId, pid);
							if (pid==process->pid) {
								printf("Killed by own kill syscall\n");
								return false;
							}
						} break;
						case ByteCodeSyscallIdGetPidRam:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidram] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdRead:
							if (process->regs[1]==process->envVars.stdioFd) {
								ssize_t result=read(STDIN_FILENO, &process->memory[process->regs[2]], process->regs[3]);
								if (result>=0)
									process->regs[0]=result;
								else
									process->regs[0]=0;
							} else
								process->regs[0]=0;

							if (infoSyscalls) {
								printf("Info: syscall(id=%i [read], fd=%u, data=%u [", syscallId, process->regs[1], process->regs[2]);
								for(int i=0; i<process->regs[0]; ++i)
									printf("%c", process->memory[process->regs[2]+i]);
								printf("], len=%u, read=%u)\n", process->regs[3], process->regs[0]);
							}
						break;
						case ByteCodeSyscallIdWrite: {
							uint8_t fd=process->regs[1];
							// uint16_t offset=process->regs[2]; // offset is ignored as we currently only allow writing to stdout
							uint16_t bufAddr=process->regs[3];
							uint16_t bufLen=process->regs[4];

							if (fd==process->envVars.stdioFd) {
								for(int i=0; i<bufLen; ++i)
									printf("%c", process->memory[bufAddr+i]);
								process->regs[0]=bufLen;
							} else
								process->regs[0]=0;

							if (infoSyscalls) {
								printf("Info: syscall(id=%i [write], fd=%u, data addr=%u [", syscallId, fd, bufAddr);
								for(int i=0; i<bufLen; ++i)
									printf("%c", process->memory[bufAddr+i]);
								printf("], len=%u, written=%u)\n", bufLen, process->regs[0]);
							}
						} break;
						case ByteCodeSyscallIdOpen:
							// TODO: file syscalls long term (handling fds etc)
							if (infoSyscalls)
								printf("Info: syscall(id=%i [open] (unimplemented)\n", syscallId);

							// This is not implemented - simply return invalid fd
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdClose:
							// TODO: see open
							if (infoSyscalls)
								printf("Info: syscall(id=%i [close] (unimplemented)\n", syscallId);
						break;
						case ByteCodeSyscallIdDirGetChildN:
							// TODO: see open
							if (infoSyscalls)
								printf("Info: syscall(id=%i [dirgetchildn] (unimplemented)\n", syscallId);
						break;
						case ByteCodeSyscallIdGetPath: {
							// TODO: see open (we could technically support stdiofd, returning /dev/ttyS0)
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpath] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						} break;
						case ByteCodeSyscallIdResizeFile:
							// TODO: see open
							if (infoSyscalls)
								printf("Info: syscall(id=%i [resizefile] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdFileGetLen:
							// TODO: see open
							if (infoSyscalls)
								printf("Info: syscall(id=%i [filegetlen] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdTryReadByte:
							// TODO: see open
							if (infoSyscalls)
								printf("Info: syscall(id=%i [tryreadbyte] (unimplemented)\n", syscallId);
							process->regs[0]=256;
						break;
						case ByteCodeSyscallIdIsDir:
							// TODO: see open
							if (infoSyscalls)
								printf("Info: syscall(id=%i [isdir] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case ByteCodeSyscallIdEnvGetStdioFd:
							process->regs[0]=process->envVars.stdioFd;
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envgetstiofd] (return fd = %u)\n", syscallId, process->regs[0]);
						break;
						case ByteCodeSyscallIdEnvSetStdioFd:
							process->envVars.stdioFd=process->regs[0];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetstdiofd], new fd %u\n", syscallId, process->envVars.stdioFd);
						break;
						case ByteCodeSyscallIdEnvGetPwd:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpwd] (unimplemented)\n", syscallId);
						break;
						case ByteCodeSyscallIdEnvSetPwd:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpwd] (unimplemented)\n", syscallId);
						break;
						case ByteCodeSyscallIdEnvGetPath:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpath] (unimplemented)\n", syscallId);
						break;
						case ByteCodeSyscallIdEnvSetPath:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpath] (unimplemented)\n", syscallId);
						break;
						case ByteCodeSyscallIdTimeMonotonic:
							// TODO: this
							if (infoSyscalls)
								printf("Info: syscall(id=%i [timemonotonic] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						default:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [unknown])\n", syscallId);
							printf("Error: Unknown syscall with id %i\n", syscallId);
							return false;
						break;
					}
				} break;
				case BytecodeInstructionMiscTypeSet8:
					process->regs[info.d.misc.d.set8.destReg]=info.d.misc.d.set8.value;
					if (infoInstructions)
						printf("Info: r%i=%u\n", info.d.misc.d.set8.destReg, info.d.misc.d.set8.value);
				break;
				case BytecodeInstructionMiscTypeSet16:
					process->regs[info.d.misc.d.set16.destReg]=info.d.misc.d.set16.value;
					if (infoInstructions)
						printf("Info: r%i=%u\n", info.d.misc.d.set16.destReg, info.d.misc.d.set16.value);
				break;
			}
		break;
	}

	++process->instructionCount;

	return true;
}

void processDebug(const Process *process) {
	printf("Info:\n");
	printf("	PID: %u\n", process->pid);
	printf("	IP: %u\n", process->regs[ByteCodeRegisterIP]);
	printf("	SP: %u\n", process->regs[ByteCodeRegisterSP]);
	printf("	Instruction count: %u\n", process->instructionCount);
	printf("	Regs:");
	for(int i=0; i<8; ++i)
		printf(" r%i=%u", i, process->regs[i]);
	printf("\n");
}
