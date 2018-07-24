#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../kernel/bytecode.h"

typedef struct {
	ByteCodeWord regs[8];

	uint8_t memory[ByteCodeMemoryTotalSize]; // combined progmem+ram

	bool skipFlag; // skip next instruction?

	unsigned instructionCount;
	ByteCodeWord pid;

	int argc;
	char **argv;
} Process;

Process *process=NULL;
bool verbose=false;
bool slow=false;

bool processRunNextInstruction(Process *process);
void processDebug(const Process *process);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc<2) {
		printf("Usage: %s [--verbose] [--slow] inputfile [inputargs ...]\n", argv[0]);
		goto done;
	}

	int i;
	for(i=1; i<argc; ++i) {
		if (strncmp(argv[i], "--", 2)!=0)
			break;
		if (strcmp(argv[i], "--verbose")==0)
			verbose=true;
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
	process->pid=(rand()&1023);
	process->argc=argc-inputArgBaseIndex;
	process->argv=argv+inputArgBaseIndex;
	const char *inputPath=argv[inputArgBaseIndex];

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

		if (verbose)
			processDebug(process);
	} while(processRunNextInstruction(process));

	// Done
	done:
	if (inputFile!=NULL)
		fclose(inputFile);
	free(process);

	return 0;
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
		if (verbose)
			printf("Error: Invalid instruction (bad bit sequence)\n");
		return false;
	}

	if (process->skipFlag) {
		if (verbose)
			printf("Info: Skipping instruction\n");
		process->skipFlag=false;
		return true;
	}

	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore:
					process->memory[process->regs[info.d.memory.destReg]]=process->regs[info.d.memory.srcReg];
					if (verbose)
						printf("Info: *r%i=r%i (*%u=%u)\n", info.d.memory.destReg, info.d.memory.srcReg, process->regs[info.d.memory.destReg], process->regs[info.d.memory.srcReg]);
				break;
				case BytecodeInstructionMemoryTypeLoad:
					process->regs[info.d.memory.destReg]=process->memory[info.d.memory.srcReg];
					if (verbose)
						printf("Info: r%i=*r%i (=%i)\n", info.d.memory.destReg, info.d.memory.srcReg, process->memory[info.d.memory.srcReg]);
				break;
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
					if (verbose) {
						if (info.d.alu.incDecValue==1)
							printf("Info: r%i++ (r%i=%i+1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
						else
							printf("Info: r%i+=%i (r%i=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.incDecValue, info.d.alu.destReg, pre, info.d.alu.incDecValue, process->regs[info.d.alu.destReg]);
					}
				} break;
				case BytecodeInstructionAluTypeDec: {
					int pre=process->regs[info.d.alu.destReg]-=info.d.alu.incDecValue;
					if (verbose) {
						if (info.d.alu.incDecValue==1)
							printf("Info: r%i-- (r%i=%i-1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
						else
							printf("Info: r%i-=%i (r%i=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.incDecValue, info.d.alu.destReg, pre, info.d.alu.incDecValue, process->regs[info.d.alu.destReg]);
					}
				} break;
				case BytecodeInstructionAluTypeAdd:
					process->regs[info.d.alu.destReg]=opA+opB;
					if (verbose)
						printf("Info: r%i=r%i+r%i (=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSub:
					process->regs[info.d.alu.destReg]=opA-opB;
					if (verbose)
						printf("Info: r%i=r%i-r%i (=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeMul:
					process->regs[info.d.alu.destReg]=opA*opB;
					if (verbose)
						printf("Info: r%i=r%i*r%i (=%i*%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeDiv:
					process->regs[info.d.alu.destReg]=opA/opB;
					if (verbose)
						printf("Info: r%i=r%i/r%i (=%i/%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeXor:
					process->regs[info.d.alu.destReg]=opA^opB;
					if (verbose)
						printf("Info: r%i=r%i^r%i (=%i^%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeOr:
					process->regs[info.d.alu.destReg]=opA|opB;
					if (verbose)
						printf("Info: r%i=r%i|r%i (=%i|%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeAnd:
					process->regs[info.d.alu.destReg]=opA&opB;
					if (verbose)
						printf("Info: r%i=r%i&r%i (=%i&%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeNot:
					process->regs[info.d.alu.destReg]=~opA;
					if (verbose)
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

					if (verbose)
						printf("Info: r%i=cmp(r%i,r%i) (=cmp(%i,%i)=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				} break;
				case BytecodeInstructionAluTypeShiftLeft:
					process->regs[info.d.alu.destReg]=opA<<opB;
					if (verbose)
						printf("Info: r%i=r%i<<r%i (=%i<<%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeShiftRight:
					process->regs[info.d.alu.destReg]=opA>>opB;
					if (verbose)
						printf("Info: r%i=r%i>>r%i (=%i>>%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSkip:
					process->skipFlag=(process->regs[info.d.alu.destReg] & (1u<<info.d.alu.opAReg));

					if (verbose)
						printf("Info: skip%u r%i (=%i, %s, skip=%i)\n", info.d.alu.opAReg, info.d.alu.destReg, process->regs[info.d.alu.destReg], byteCodeInstructionAluCmpBitStrings[info.d.alu.opAReg], process->skipFlag);
				break;
				case BytecodeInstructionAluTypeStore16:
					process->memory[process->regs[info.d.alu.destReg]]=(process->regs[info.d.alu.opAReg]>>8);
					process->memory[process->regs[info.d.alu.destReg]+1]=(process->regs[info.d.alu.opAReg]&0xFF);
					if (verbose)
						printf("Info: [r%i]=r%i (16 bit) ([%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, process->regs[info.d.alu.destReg], opA);
				break;
				case BytecodeInstructionAluTypeLoad16:
					process->regs[info.d.alu.destReg]=(((ByteCodeWord)process->memory[process->regs[info.d.alu.opAReg]])<<8) |
					                                   process->memory[process->regs[info.d.alu.opAReg]+1];
					if (verbose)
						printf("Info: r%i=[r%i] (16 bit) (=[%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, opA, process->regs[info.d.alu.destReg]);
				break;
			}
		} break;
		case BytecodeInstructionTypeMisc:
			switch(info.d.misc.type) {
				case BytecodeInstructionMiscTypeNop:
					if (verbose)
						printf("Info: nop\n");
				break;
				case BytecodeInstructionMiscTypeSyscall: {
					uint16_t syscallId=process->regs[0];
					switch(syscallId) {
						case ByteCodeSyscallIdExit:
							if (verbose)
								printf("Info: syscall(id=%i [exit], status=%u)\n", syscallId, process->regs[1]);
							return false;
						break;
						case ByteCodeSyscallIdGetPid: {
							if (verbose)
								printf("Info: syscall(id=%i [getpid], return=%u)\n", syscallId, process->pid);

							process->regs[0]=process->pid;
						} break;
						case ByteCodeSyscallIdGetArgC: {
							if (verbose)
								printf("Info: syscall(id=%i [getargc], return=%u)\n", syscallId, process->argc);

							process->regs[0]=process->argc;
						} break;
						case ByteCodeSyscallIdGetArgVN: {
							// TODO: Check n is sensible

							int n=process->regs[1];
							int buf=process->regs[2];
							int len=process->regs[3];

							const char *str=process->argv[n];
							int trueLen=strlen(str);

							if (verbose)
								printf("Info: syscall(id=%i [getargvn], n=%i, buf=%i, len=%i, return=%u with '%s')\n", syscallId, n, buf, len, trueLen, str);

							for(int i=0; i<len && i<trueLen; ++i)
								process->memory[buf+i]=str[i];
							process->regs[0]=trueLen;
						} break;
						case ByteCodeSyscallIdRead:
							if (process->regs[1]==ByteCodeFdStdin) {
								ssize_t result=read(STDIN_FILENO, &process->memory[process->regs[2]], process->regs[3]);
								if (result>=0)
									process->regs[0]=result;
								else
									process->regs[0]=0;
							} else
								process->regs[0]=0;

							if (verbose) {
								printf("Info: syscall(id=%i [read], fd=%u, data=%u [", syscallId, process->regs[1], process->regs[2]);
								for(int i=0; i<process->regs[0]; ++i)
									printf("%c", process->memory[process->regs[2]+i]);
								printf("], len=%u, read=%u)\n", process->regs[3], process->regs[0]);
							}
						break;
						case ByteCodeSyscallIdWrite:
							if (process->regs[1]==ByteCodeFdStdout) {
								for(int i=0; i<process->regs[3]; ++i)
									printf("%c", process->memory[process->regs[2]+i]);
								process->regs[0]=process->regs[3];
							} else
								process->regs[0]=0;

							if (verbose) {
								printf("Info: syscall(id=%i [write], fd=%u, data=%u [", syscallId, process->regs[1], process->regs[2]);
								for(int i=0; i<process->regs[3]; ++i)
									printf("%c", process->memory[process->regs[2]+i]);
								printf("], len=%u, written=%u)\n", process->regs[3], process->regs[0]);
							}
						break;
						default:
							if (verbose)
								printf("Info: syscall(id=%i [unknown])\n", syscallId);
							printf("Error: Unknown syscall with id %i\n", syscallId);
							return false;
						break;
					}
				} break;
				case BytecodeInstructionMiscTypeSet8:
					process->regs[info.d.misc.d.set8.destReg]=info.d.misc.d.set8.value;
					if (verbose)
						printf("Info: r%i=%u\n", info.d.misc.d.set8.destReg, info.d.misc.d.set8.value);
				break;
				case BytecodeInstructionMiscTypeSet16:
					process->regs[info.d.misc.d.set16.destReg]=info.d.misc.d.set16.value;
					if (verbose)
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
