#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"

void statsPrint(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, ...);
void statsPrintV(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, va_list ap);

const char *statsBytecodeInstructionAluTypeToString(BytecodeInstructionAluType type);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc<2) {
		printf("Usage: %s inputfile1 [inputfile2 [...]]\n", argv[0]);
		return 0;
	}

	// Init statistics
	unsigned instructionCount=0;
	unsigned byteCount=0;

	unsigned instructionSet16Count=0;
	unsigned instructionSet16Less256Count=0;
	unsigned instructionSet16Less16Count=0;
	unsigned instructionSet8Count=0;
	unsigned instructionSet8Less16Count=0;

	unsigned instructionAluCount=0;
	unsigned instructionAluCounts[BytecodeInstructionAluTypeExtra+1]; // HACK
	for(unsigned i=0; i<BytecodeInstructionAluTypeExtra+1; ++i)
		instructionAluCounts[i]=0;

	unsigned instructionLoad8Count=0;
	unsigned instructionStore8Count=0;
	unsigned instructionXchg8Count=0;

	unsigned instructionNopCount=0;
	unsigned instructionSyscallCount=0;

	// Loop over given files
	for(unsigned arg=1; arg<argc; ++arg) {
		// Open input file
		const char *inputPath=argv[arg];
		inputFile=fopen(inputPath, "r");
		if (inputFile==NULL) {
			printf("Could not open input file '%s' for reading\n", inputPath);
			continue;
		}

		// Parse input file
		unsigned addr=0;
		int c;
		while((c=fgetc(inputFile))!=EOF) {
			// First see if we need to load another byte for a long instruction
			BytecodeInstructionLong instruction;
			instruction[0]=c;
			BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
			if (length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong) {
				c=fgetc(inputFile);
				if (c==EOF) {
					statsPrint(addr, instruction, "Missing 2nd byte of instruction");
					break;
				}
				instruction[1]=c;
			}
			if (length==BytecodeInstructionLengthLong) {
				c=fgetc(inputFile);
				if (c==EOF) {
					statsPrint(addr, instruction, "Missing 3rd byte of instruction");
					break;
				}
				instruction[2]=c;
			}

			// Parse instruction
			BytecodeInstructionInfo info;
			if (!bytecodeInstructionParse(&info, instruction)) {
				if (length==BytecodeInstructionLengthShort)
					statsPrint(addr, instruction, "Unknown short instruction");
				else
					statsPrint(addr, instruction, "Unknown long instruction");
			} else {
				switch(info.type) {
					case BytecodeInstructionTypeMemory:
						switch(info.d.memory.type) {
							case BytecodeInstructionMemoryTypeStore8:
								//statsPrint(addr, instruction, "*r%u=r%u", info.d.memory.destReg, info.d.memory.srcReg);
								++instructionStore8Count;
							break;
							case BytecodeInstructionMemoryTypeLoad8:
								//statsPrint(addr, instruction, "r%u=*r%u", info.d.memory.destReg, info.d.memory.srcReg);
								++instructionLoad8Count;
							break;
							case BytecodeInstructionMemoryTypeXchg8:
								//statsPrint(addr, instruction, "xchg8 *r%u r%u", info.d.memory.destReg, info.d.memory.srcReg);
								++instructionXchg8Count;
							break;
						}
					break;
					case BytecodeInstructionTypeAlu:
						++instructionAluCount;
						++instructionAluCounts[info.d.alu.type];
						/*
						switch(info.d.alu.type) {
							case BytecodeInstructionAluTypeInc:
								if (info.d.alu.incDecValue==1)
									statsPrint(addr, instruction, "r%u++", info.d.alu.destReg);
								else
									statsPrint(addr, instruction, "r%u+=%u", info.d.alu.destReg, info.d.alu.incDecValue);
							break;
							case BytecodeInstructionAluTypeDec:
								if (info.d.alu.incDecValue==1)
									statsPrint(addr, instruction, "r%u--", info.d.alu.destReg);
								else
									statsPrint(addr, instruction, "r%u-=%u", info.d.alu.destReg, info.d.alu.incDecValue);
							break;
							case BytecodeInstructionAluTypeAdd:
								statsPrint(addr, instruction, "r%u=r%u+r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeSub:
								statsPrint(addr, instruction, "r%u=r%u-r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeMul:
								statsPrint(addr, instruction, "r%u=r%u*r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeDiv:
								statsPrint(addr, instruction, "r%u=r%u/r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeXor:
								statsPrint(addr, instruction, "r%u=r%u^r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeOr:
								statsPrint(addr, instruction, "r%u=r%u|r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeAnd:
								statsPrint(addr, instruction, "r%u=r%u&r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeNot:
								statsPrint(addr, instruction, "r%u=~r%u", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluTypeCmp:
								statsPrint(addr, instruction, "r%u=cmp(r%u, r%u)", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeShiftLeft:
								statsPrint(addr, instruction, "r%u=r%u<<r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeShiftRight:
								statsPrint(addr, instruction, "r%u=r%u>>r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
							break;
							case BytecodeInstructionAluTypeSkip:
								statsPrint(addr, instruction, "skip%u r%u (%s), dist %u", info.d.alu.opAReg, info.d.alu.destReg, byteCodeInstructionAluCmpBitStrings[info.d.alu.opAReg], info.d.alu.opBReg+1);
							break;
							case BytecodeInstructionAluTypeStore16:
								statsPrint(addr, instruction, "[r%u]=r%u (16 bit)", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluTypeLoad16:
								statsPrint(addr, instruction, "r%u=[r%u] (16 bit)", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							default:
								statsPrint(addr, instruction, "unknown ALU operation");
							break;
						}
						*/
					break;
					case BytecodeInstructionTypeMisc:
						switch(info.d.misc.type) {
							case BytecodeInstructionMiscTypeNop:
								//statsPrint(addr, instruction, "nop");
								++instructionNopCount;
							break;
							case BytecodeInstructionMiscTypeSyscall:
								//statsPrint(addr, instruction, "syscall");
								++instructionSyscallCount;
							break;
							case BytecodeInstructionMiscTypeSet8:
								//statsPrint(addr, instruction, "r%u=%u", info.d.misc.d.set8.destReg, info.d.misc.d.set8.value);
								++instructionSet8Count;
								if (info.d.misc.d.set8.value<16)
									++instructionSet8Less16Count;
							break;
							case BytecodeInstructionMiscTypeSet16:
								//statsPrint(addr, instruction, "r%u=%u", info.d.misc.d.set16.destReg, info.d.misc.d.set16.value);
								++instructionSet16Count;
								if (info.d.misc.d.set16.value<256)
									++instructionSet16Less256Count;
								if (info.d.misc.d.set16.value<16)
									++instructionSet16Less16Count;
							break;
							case BytecodeInstructionMiscTypeClearInstructionCache:
							break;
						}
					break;
				}
			}

			++instructionCount;

			// Update addr for next call
			addr+=1+(length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)+(length==BytecodeInstructionLengthLong);
		}

		byteCount+=addr;

		// Close file
		fclose(inputFile);
	}

	// Print stats
	printf("Statistics:\n\n");

	printf("instructions: total %u\n", instructionCount);
	printf("bytes: total %u\n", byteCount);
	printf("\n");

	printf("set8: total %u, <16 %u\n", instructionSet8Count, instructionSet8Less16Count);
	printf("set16: total %u, <256 %u, <16 %u\n", instructionSet16Count, instructionSet16Less256Count, instructionSet16Less16Count);
	printf("\n");

	printf("instructionLoad8Count: total %u\n", instructionLoad8Count);
	printf("instructionStore8Count: total %u\n", instructionStore8Count);
	printf("instructionXchg8Count: total %u\n", instructionXchg8Count);
	printf("\n");

	printf("instructionAluCount: total %u\n", instructionAluCount);
	for(unsigned i=0; i<BytecodeInstructionAluTypeExtra+1; ++i) {
		printf("	%2u (%s): total %u\n", i, statsBytecodeInstructionAluTypeToString(i), instructionAluCounts[i]);
	}
	printf("\n");

	printf("instructionSyscallCount: total %u\n", instructionSyscallCount);
	printf("instructionNopCount: total %u\n", instructionNopCount);
	printf("\n");

	return 0;
}

void statsPrint(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	statsPrintV(addr, instruction, fmt, ap);
	va_end(ap);
}

void statsPrintV(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, va_list ap) {
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	char c0=(isgraph(instruction[0]) ? (instruction[0]) : '.');
	char c1=(isgraph(instruction[1]) ? (instruction[1]) : '.');
	char c2=(isgraph(instruction[2]) ? (instruction[2]) : '.');
	switch(length) {
		case BytecodeInstructionLengthShort:
			printf("%06u=%04X      %02X=%c   ", addr, addr, (instruction[0]), c0);
		break;
		case BytecodeInstructionLengthStandard:
			printf("%06u=%04X    %02X%02X=%c%c  ", addr, addr, instruction[0], instruction[1], c0, c1);
		break;
		case BytecodeInstructionLengthLong:
			printf("%06u=%04X  %02X%02X%02X=%c%c%c ", addr, addr, instruction[0], instruction[1], instruction[2], c0, c1, c2);
		break;
	}

	vprintf(fmt, ap);

	printf("\n");
}

const char *statsBytecodeInstructionAluTypeStrings[BytecodeInstructionAluTypeExtra+1]={ // HACK
	[BytecodeInstructionAluTypeInc]="Inc",
	[BytecodeInstructionAluTypeDec]="Dec",
	[BytecodeInstructionAluTypeAdd]="Add",
	[BytecodeInstructionAluTypeSub]="Sub",
	[BytecodeInstructionAluTypeMul]="Mul",
	[BytecodeInstructionAluTypeDiv]="Div",
	[BytecodeInstructionAluTypeXor]="Xor",
	[BytecodeInstructionAluTypeOr]="Or",
	[BytecodeInstructionAluTypeAnd]="And",
	[BytecodeInstructionAluTypeCmp]="Cmp",
	[BytecodeInstructionAluTypeShiftLeft]="ShiftLeft",
	[BytecodeInstructionAluTypeShiftRight]="ShiftRight",
	[BytecodeInstructionAluTypeSkip]="Skip",
	[BytecodeInstructionAluTypeExtra]="Extra",
};

const char *statsBytecodeInstructionAluTypeToString(BytecodeInstructionAluType type) {
	return statsBytecodeInstructionAluTypeStrings[type];
}
