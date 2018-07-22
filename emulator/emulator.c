#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/bytecode.h"

typedef struct {
	ByteCodeWord regs[8];

	uint8_t progmem[65536];
	uint8_t ram[65536];
} Process;

Process *process=NULL;
bool verbose=false;

bool processRunNextInstruction(Process *process);
void processDebug(const Process *process);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc!=2 && argc!=3) {
		printf("Usage: %s inputfile [--verbose]\n", argv[0]);
		goto done;
	}

	const char *inputPath=argv[1];
	verbose=(argc>2 && strcmp(argv[2], "--verbose")==0);

	// Allocate process data struct
	process=malloc(sizeof(Process));
	if (process==NULL) {
		printf("Could not allocate process data struct\n");
		goto done;
	}

	process->regs[ByteCodeRegisterIP]=0;

	// Read-in input file
	inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		goto done;
	}

	int c;
	uint8_t *next=process->progmem;
	while((c=fgetc(inputFile))!=EOF)
		*next++=c;

	// Run process
	do {
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
	BytecodeInstructionLong instruction=(((BytecodeInstructionLong)process->progmem[process->regs[ByteCodeRegisterIP]++])<<8);
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	if (length==BytecodeInstructionLengthLong)
		instruction|=process->progmem[process->regs[ByteCodeRegisterIP]++];

	BytecodeInstructionInfo info;
	if (!bytecodeInstructionParse(&info, instruction)) {
		if (verbose)
			printf("Invalid instruction\n");
		return false;
	}

	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore:
					process->ram[process->regs[info.d.memory.destReg]]=process->regs[info.d.memory.srcReg];
					if (verbose)
						printf("*r%i=r%i (*%u=%u)\n", info.d.memory.destReg, info.d.memory.srcReg, process->regs[info.d.memory.destReg], process->regs[info.d.memory.srcReg]);
				break;
				case BytecodeInstructionMemoryTypeLoad:
					process->regs[info.d.memory.destReg]=process->ram[info.d.memory.srcReg];
					if (verbose)
						printf("r%i=*r%i (=%i)\n", info.d.memory.destReg, info.d.memory.srcReg, process->ram[info.d.memory.srcReg]);
				break;
				case BytecodeInstructionMemoryTypeLoadProgmem:
					process->regs[info.d.memory.destReg]=process->progmem[info.d.memory.srcReg];
					if (verbose)
						printf("r%i=PROGMEM[r%i] (=%i)\n", info.d.memory.destReg, info.d.memory.srcReg, process->progmem[info.d.memory.srcReg]);
				break;
			}
		break;
		case BytecodeInstructionTypeAlu: {
			int opA=process->regs[info.d.alu.opAReg];
			int opB=process->regs[info.d.alu.opBReg];
			switch(info.d.alu.type) {
				case BytecodeInstructionAluTypeInc: {
					int pre=process->regs[info.d.alu.destReg]++;
					if (verbose)
						printf("r%i++ (r%i=%i+1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
				} break;
				case BytecodeInstructionAluTypeDec: {
					int pre=process->regs[info.d.alu.destReg]--;
					if (verbose)
						printf("r%i-- (r%i=%i-1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
				} break;
				case BytecodeInstructionAluTypeAdd:
					process->regs[info.d.alu.destReg]=opA+opB;
					if (verbose)
						printf("r%i=r%i+r%i (=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSub:
					process->regs[info.d.alu.destReg]=opA-opB;
					if (verbose)
						printf("r%i=r%i-r%i (=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeMul:
					process->regs[info.d.alu.destReg]=opA*opB;
					if (verbose)
						printf("r%i=r%i*r%i (=%i*%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeXor:
					process->regs[info.d.alu.destReg]=opA^opB;
					if (verbose)
						printf("r%i=r%i^r%i (=%i^%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeOr:
					process->regs[info.d.alu.destReg]=opA|opB;
					if (verbose)
						printf("r%i=r%i|r%i (=%i|%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeAnd:
					process->regs[info.d.alu.destReg]=opA&opB;
					if (verbose)
						printf("r%i=r%i&r%i (=%i&%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeNot:
					process->regs[info.d.alu.destReg]=~opA;
					if (verbose)
						printf("r%i=~r%i (=~%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, opA, process->regs[info.d.alu.destReg]);
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
				} break;
				case BytecodeInstructionAluTypeShiftLeft:
					process->regs[info.d.alu.destReg]=opA<<opB;
					if (verbose)
						printf("r%i=r%i<<r%i (=%i<<%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeShiftRight:
					process->regs[info.d.alu.destReg]=opA>>opB;
					if (verbose)
						printf("r%i=r%i>>r%i (=%i>>%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
			}
		} break;
		case BytecodeInstructionTypeMisc:
			switch(info.d.misc.type) {
				case BytecodeInstructionMiscTypeNop:
					if (verbose)
						printf("nop\n");
				break;
				case BytecodeInstructionMiscTypeSyscall: {
					uint16_t syscallId=process->regs[0];
					switch(syscallId) {
						case 1:
							// write progmem
							if (verbose) {
								printf("syscall(id=%i [writeprogmem], fd=%u, data=%u [", syscallId, process->regs[1], process->regs[2]);
								for(int i=0; i<process->regs[3]; ++i)
									printf("%c", process->progmem[process->regs[2]+i]);
								printf("], len=%u)\n", process->regs[3]);
							}

							for(int i=0; i<process->regs[3]; ++i)
								printf("%c", process->progmem[process->regs[2]+i]);
						break;
						case 2:
							if (verbose)
								printf("syscall(id=%i [exit], status=%u)\n", syscallId, process->regs[1]);
							return false;
						break;
						default:
							if (verbose)
								printf("syscall(id=%i [unknown])\n", syscallId);
							return false;
						break;
					}
				} break;
				case BytecodeInstructionMiscTypeSet:
					process->regs[info.d.misc.d.set.destReg]=info.d.misc.d.set.value;
					if (verbose)
						printf("r%i=%u\n", info.d.misc.d.set.destReg, info.d.misc.d.set.value);
				break;
			}
		break;
	}

	return true;
}

void processDebug(const Process *process) {
	printf("Process %p:\n", process);
	printf("	IP: %u\n", process->regs[ByteCodeRegisterIP]);
	printf("	Regs:");
	for(int i=0; i<8; ++i)
		printf(" r%i=%u", i, process->regs[i]);
	printf("\n");
}
