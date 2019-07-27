#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"

void statsPrint(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, ...);
void statsPrintV(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, va_list ap);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc!=2) {
		printf("Usage: %s inputfile\n", argv[0]);
		goto done;
	}

	const char *inputPath=argv[1];

	// Open input file
	inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		goto done;
	}

	// Init statistics
	unsigned instructionSet16Count=0;
	unsigned instructionSet16Less256Count=0;
	unsigned instructionSet16Less16Count=0;
	unsigned instructionSet8Count=0;
	unsigned instructionSet8Less16Count=0;

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
					/*
					switch(info.d.memory.type) {
						case BytecodeInstructionMemoryTypeStore8:
							statsPrint(addr, instruction, "*r%u=r%u", info.d.memory.destReg, info.d.memory.srcReg);
						break;
						case BytecodeInstructionMemoryTypeLoad8:
							statsPrint(addr, instruction, "r%u=*r%u", info.d.memory.destReg, info.d.memory.srcReg);
						break;
						case BytecodeInstructionMemoryTypeXchg8:
							statsPrint(addr, instruction, "xchg8 *r%u r%u", info.d.memory.destReg, info.d.memory.srcReg);
						break;
					}
					*/
				break;
				case BytecodeInstructionTypeAlu:
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
						break;
						case BytecodeInstructionMiscTypeSyscall:
							//statsPrint(addr, instruction, "syscall");
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
					}
				break;
			}
		}

		// Update addr for next call
		addr+=1+(length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)+(length==BytecodeInstructionLengthLong);
	}

	// Print stats
	printf("Statistics:\n\n");

	printf("set8: total %u, <16 %u\n", instructionSet8Count, instructionSet8Less16Count);
	printf("set16: total %u, <256 %u, <16 %u\n", instructionSet16Count, instructionSet16Less256Count, instructionSet16Less16Count);
	printf("\n");

	// Done
	done:
	if (inputFile!=NULL)
		fclose(inputFile);

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
