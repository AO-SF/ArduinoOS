#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/bytecode.h"

void disassemblerPrint(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, ...);
void disassemblerPrintV(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, va_list ap);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc!=2) {
		printf("Usage: %s inputfile\n", argv[0]);
		goto done;
	}

	const char *inputPath=argv[1];

	// Read input file
	inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		goto done;
	}

	printf("Addr Instruction Description\n");
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
				disassemblerPrint(addr, instruction, "Missing 2nd byte of instruction");
				break;
			}
			instruction[1]=c;
		}
		if (length==BytecodeInstructionLengthLong) {
			c=fgetc(inputFile);
			if (c==EOF) {
				disassemblerPrint(addr, instruction, "Missing 3rd byte of instruction");
				break;
			}
			instruction[2]=c;
		}

		// Parse instruction
		BytecodeInstructionInfo info;
		if (!bytecodeInstructionParse(&info, instruction)) {
			if (length==BytecodeInstructionLengthShort)
				disassemblerPrint(addr, instruction, "Unknown short instruction");
			else
				disassemblerPrint(addr, instruction, "Unknown long instruction");
		} else {
			switch(info.type) {
				case BytecodeInstructionTypeMemory:
					switch(info.d.memory.type) {
						case BytecodeInstructionMemoryTypeStore:
							disassemblerPrint(addr, instruction, "*r%u=r%u", info.d.memory.destReg, info.d.memory.srcReg);
						break;
						case BytecodeInstructionMemoryTypeLoad:
							disassemblerPrint(addr, instruction, "r%u=*r%u", info.d.memory.destReg, info.d.memory.srcReg);
						break;
						case BytecodeInstructionMemoryTypeLoadProgmem:
							disassemblerPrint(addr, instruction, "r%u=PROGMEM[r%u]", info.d.memory.destReg, info.d.memory.srcReg);
						break;
					}
				break;
				case BytecodeInstructionTypeAlu:
					switch(info.d.alu.type) {
						case BytecodeInstructionAluTypeInc:
							disassemblerPrint(addr, instruction, "r%u++", info.d.alu.destReg);
						break;
						case BytecodeInstructionAluTypeDec:
							disassemblerPrint(addr, instruction, "r%u--", info.d.alu.destReg);
						break;
						case BytecodeInstructionAluTypeAdd:
							disassemblerPrint(addr, instruction, "r%u=r%u+r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeSub:
							disassemblerPrint(addr, instruction, "r%u=r%u-r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeMul:
							disassemblerPrint(addr, instruction, "r%u=r%u*r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeXor:
							disassemblerPrint(addr, instruction, "r%u=r%u^r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeOr:
							disassemblerPrint(addr, instruction, "r%u=r%u|r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeAnd:
							disassemblerPrint(addr, instruction, "r%u=r%u&r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeNot:
							disassemblerPrint(addr, instruction, "r%u=~r%u", info.d.alu.destReg, info.d.alu.opAReg);
						break;
						case BytecodeInstructionAluTypeCmp:
							disassemblerPrint(addr, instruction, "r%u=cmp(r%u, r%u)", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeShiftLeft:
							disassemblerPrint(addr, instruction, "r%u=r%u<<r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeShiftRight:
							disassemblerPrint(addr, instruction, "r%u=r%u>>r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
						break;
						case BytecodeInstructionAluTypeSkip:
							disassemblerPrint(addr, instruction, "skip%u r%u (%s)", info.d.alu.opAReg, info.d.alu.destReg, byteCodeInstructionAluCmpBitStrings[info.d.alu.opAReg]);
						break;
						default:
							disassemblerPrint(addr, instruction, "unknown ALU operation");
						break;
					}
				break;
				case BytecodeInstructionTypeMisc:
					switch(info.d.misc.type) {
						case BytecodeInstructionMiscTypeNop:
							disassemblerPrint(addr, instruction, "nop");
						break;
						case BytecodeInstructionMiscTypeSyscall:
							disassemblerPrint(addr, instruction, "syscall");
						break;
						case BytecodeInstructionMiscTypeSet8:
							disassemblerPrint(addr, instruction, "r%u=%u", info.d.misc.d.set8.destReg, info.d.misc.d.set8.value);
						break;
						case BytecodeInstructionMiscTypeSet16:
							disassemblerPrint(addr, instruction, "r%u=%u", info.d.misc.d.set16.destReg, info.d.misc.d.set16.value);
						break;
					}
				break;
			}
		}

		// Update addr for next call
		addr+=1+(length==BytecodeInstructionLengthStandard || length==BytecodeInstructionLengthLong)+(length==BytecodeInstructionLengthLong);
	}

	// Done
	done:
	if (inputFile!=NULL)
		fclose(inputFile);

	return 0;
}

void disassemblerPrint(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	disassemblerPrintV(addr, instruction, fmt, ap);
	va_end(ap);
}

void disassemblerPrintV(uint16_t addr, BytecodeInstructionLong instruction, const char *fmt, va_list ap) {
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	char c0=(isgraph(instruction[0]) ? (instruction[0]) : '.');
	char c1=(isgraph(instruction[1]) ? (instruction[1]) : '.');
	char c2=(isgraph(instruction[2]) ? (instruction[2]) : '.');
	switch(length) {
		case BytecodeInstructionLengthShort:
			printf("%04X      %02X=%c   ", addr, (instruction[0]), c0);
		break;
		case BytecodeInstructionLengthStandard:
			printf("%04X    %02X%02X=%c%c  ", addr, instruction[0], instruction[1], c0, c1);
		break;
		case BytecodeInstructionLengthLong:
			printf("%04X  %02X%02X%02X=%c%c%c ", addr, instruction[0], instruction[1], instruction[2], c0, c1, c2);
		break;
	}

	vprintf(fmt, ap);

	printf("\n");
}
