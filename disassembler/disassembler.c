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

	printf("ADDR INSTRUC Description\n");
	unsigned addr=0;
	int c;
	while((c=fgetc(inputFile))!=EOF) {
		// First see if we need to load another byte for a long instruction
		BytecodeInstructionLong instruction=(((uint16_t)c)<<8)|0;
		BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
		if (length==BytecodeInstructionLengthLong) {
			c=fgetc(inputFile);
			if (c==EOF) {
				disassemblerPrint(addr, instruction, "Missing lower half of final instruction");
				break;
			}
			instruction|=(uint8_t)c;
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
						case BytecodeInstructionMiscTypeSet:
							disassemblerPrint(addr, instruction, "r%u=%u", info.d.misc.d.set.destReg, info.d.misc.d.set.value);
						break;
					}
				break;
			}
		}

		// Update addr for next call
		addr+=1+(length==BytecodeInstructionLengthLong);
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
	if (length==BytecodeInstructionLengthShort) {
		char c1=(isgraph(instruction>>8) ? (instruction>>8) : '.');
		printf("%04X %02X..=%c. ", addr, (instruction>>8), c1);
	} else {
		char c1=(isgraph(instruction>>8) ? (instruction>>8) : '.');
		char c2=(isgraph(instruction&0xFF) ? (instruction&0xFF) : '.');
		printf("%04X %04X=%c%c ", addr, instruction, c1, c2);
	}

	vprintf(fmt, ap);

	printf("\n");
}
