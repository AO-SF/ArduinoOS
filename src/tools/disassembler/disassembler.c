#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "profile.h"

// Global hacks for ease
FILE *inputFile=NULL;
FILE *profileFile=NULL;

void disassemblerPrint(uint16_t addr, BytecodeInstruction3Byte instruction, const char *fmt, ...);
void disassemblerPrintV(uint16_t addr, BytecodeInstruction3Byte instruction, const char *fmt, va_list ap);

int main(int argc, char **argv) {
	// Parse arguments
	if (argc!=2 && argc!=3) {
		printf("Usage: %s inputfile [profilefile]\n", argv[0]);
		goto done;
	}

	const char *inputPath=argv[1];
	const char *profilePath=(argc==3 ? argv[2] : NULL);

	// Read input file
	inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		goto done;
	}

	// Read profile file if given
	if (profilePath!=NULL) {
		profileFile=fopen(profilePath, "r");
		if (profileFile==NULL)
			printf("Warning: Could not open profile file '%s' for reading\n", profilePath);
	}

	// Parse instructions
	printf("       Addr Instruction Description\n");
	unsigned addr=0;
	int c;
	while((c=fgetc(inputFile))!=EOF) {
		// First see if we need to load another byte for a long instruction
		BytecodeInstruction3Byte instruction;
		instruction[0]=c;
		BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
		if (length==BytecodeInstructionLength2Byte || length==BytecodeInstructionLength3Byte) {
			c=fgetc(inputFile);
			if (c==EOF) {
				disassemblerPrint(addr, instruction, "Missing 2nd byte of instruction");
				break;
			}
			instruction[1]=c;
		}
		if (length==BytecodeInstructionLength3Byte) {
			c=fgetc(inputFile);
			if (c==EOF) {
				disassemblerPrint(addr, instruction, "Missing 3rd byte of instruction");
				break;
			}
			instruction[2]=c;
		}

		// Parse instruction
		BytecodeInstructionInfo info;
		bytecodeInstructionParse(&info, instruction);
		switch(info.type) {
			case BytecodeInstructionTypeMemory:
				switch(info.d.memory.type) {
					case BytecodeInstructionMemoryTypeStore8:
						disassemblerPrint(addr, instruction, "*r%u=r%u", info.d.memory.destReg, info.d.memory.srcReg);
					break;
					case BytecodeInstructionMemoryTypeLoad8:
						disassemblerPrint(addr, instruction, "r%u=*r%u", info.d.memory.destReg, info.d.memory.srcReg);
					break;
					case BytecodeInstructionMemoryTypeSet4:
						disassemblerPrint(addr, instruction, "r%u=%u", info.d.memory.destReg, info.d.memory.set4Value);
					break;
				}
			break;
			case BytecodeInstructionTypeAlu:
				switch(info.d.alu.type) {
					case BytecodeInstructionAluTypeInc:
						if (info.d.alu.incDecValue==1)
							disassemblerPrint(addr, instruction, "r%u++", info.d.alu.destReg);
						else
							disassemblerPrint(addr, instruction, "r%u+=%u", info.d.alu.destReg, info.d.alu.incDecValue);
					break;
					case BytecodeInstructionAluTypeDec:
						if (info.d.alu.incDecValue==1)
							disassemblerPrint(addr, instruction, "r%u--", info.d.alu.destReg);
						else
							disassemblerPrint(addr, instruction, "r%u-=%u", info.d.alu.destReg, info.d.alu.incDecValue);
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
					case BytecodeInstructionAluTypeDiv:
						disassemblerPrint(addr, instruction, "r%u=r%u/r%u", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg);
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
						disassemblerPrint(addr, instruction, "skip%u r%u (%s), dist %u", info.d.alu.opAReg, info.d.alu.destReg, byteCodeInstructionAluCmpBitStrings[info.d.alu.opAReg], info.d.alu.opBReg+1);
					break;
					case BytecodeInstructionAluTypeExtra: {
						switch(info.d.alu.opBReg) {
							case BytecodeInstructionAluExtraTypeNot:
								disassemblerPrint(addr, instruction, "r%u=~r%u", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluExtraTypeStore16:
								disassemblerPrint(addr, instruction, "[r%u]=r%u (16 bit)", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluExtraTypeLoad16:
								disassemblerPrint(addr, instruction, "r%u=[r%u] (16 bit)", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluExtraTypePush16:
								disassemblerPrint(addr, instruction, "[r%u]=r%u, r%u+=2 (16 bit push)", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.destReg);
							break;
							case BytecodeInstructionAluExtraTypePop16:
								disassemblerPrint(addr, instruction, "r%u-=2, r%u=[r%u] (16 bit pop)", info.d.alu.opAReg, info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluExtraTypeCall:
								disassemblerPrint(addr, instruction, "call r%u r%u (call)", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							case BytecodeInstructionAluExtraTypeXchg8:
								disassemblerPrint(addr, instruction, "xchg8 *r%u r%u", info.d.alu.destReg, info.d.alu.opAReg);
							break;
							default:
								disassemblerPrint(addr, instruction, "unknown ALU extra operation (type %u)", info.d.alu.opBReg);
							break;
						}
					} break;
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
					case BytecodeInstructionMiscTypeClearInstructionCache:
						disassemblerPrint(addr, instruction, "clear icache");
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

		// Update addr for next call
		addr+=1+(length==BytecodeInstructionLength2Byte || length==BytecodeInstructionLength3Byte)+(length==BytecodeInstructionLength3Byte);
	}

	// Done
	done:
	if (inputFile!=NULL)
		fclose(inputFile);
	if (profileFile!=NULL)
		fclose(profileFile);

	return 0;
}

void disassemblerPrint(uint16_t addr, BytecodeInstruction3Byte instruction, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	disassemblerPrintV(addr, instruction, fmt, ap);
	va_end(ap);
}

void disassemblerPrintV(uint16_t addr, BytecodeInstruction3Byte instruction, const char *fmt, va_list ap) {
	// Print instruction
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	char c0=(isgraph(instruction[0]) ? (instruction[0]) : '.');
	char c1=(isgraph(instruction[1]) ? (instruction[1]) : '.');
	char c2=(isgraph(instruction[2]) ? (instruction[2]) : '.');
	switch(length) {
		case BytecodeInstructionLength1Byte:
			printf("%06u=%04X      %02X=%c   ", addr, addr, (instruction[0]), c0);
		break;
		case BytecodeInstructionLength2Byte:
			printf("%06u=%04X    %02X%02X=%c%c  ", addr, addr, instruction[0], instruction[1], c0, c1);
		break;
		case BytecodeInstructionLength3Byte:
			printf("%06u=%04X  %02X%02X%02X=%c%c%c ", addr, addr, instruction[0], instruction[1], instruction[2], c0, c1, c2);
		break;
	}

	// Read profiling byte if file open
	if (profileFile!=NULL) {
		if (fseek(profileFile, sizeof(ProfileCounter)*addr, SEEK_SET)==0) {
			ProfileCounter profileCount=0;
			if (fread(&profileCount, sizeof(ProfileCounter), 1, profileFile)==1) {
				printf("%4ux ", profileCount);
			}
		}
	}

	// Print user string
	vprintf(fmt, ap);

	// Terminate with new line
	printf("\n");
}
