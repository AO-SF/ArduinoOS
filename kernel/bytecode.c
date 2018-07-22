#include "bytecode.h"

BytecodeInstructionLength bytecodeInstructionParseLength(BytecodeInstructionLong instruction) {
	if ((instruction>>12)>=0xD)
		return BytecodeInstructionLengthLong;
	else
		return BytecodeInstructionLengthShort;
}

bool bytecodeInstructionParse(BytecodeInstructionInfo *info, BytecodeInstructionLong instruction) {
	// If first two bits are not both 1 then we are dealing with a memory-type instruction.
	uint8_t upperTwoBits=(instruction>>14);
	if (upperTwoBits!=0x3) {
		info->type=BytecodeInstructionTypeMemory;
		info->length=BytecodeInstructionLengthShort;
		info->d.memory.type=upperTwoBits;
		info->d.memory.destReg=(instruction>>11)&0x7;
		info->d.memory.srcReg=(instruction>>8)&0x7;
		return true;
	}

	// Otherwise if the 3rd bit is also 1 then this is an ALU instruction
	if (((instruction>>13)&1)==1) {
		info->type=BytecodeInstructionTypeAlu;
		info->length=BytecodeInstructionLengthLong;

		info->d.alu.type=((instruction>>9)&0xF);
		info->d.alu.destReg=((instruction>>6)&0x7);
		info->d.alu.opAReg=((instruction>>3)&0x7);
		info->d.alu.opBReg=(instruction&0x7);

		return true;
	}

	// Otherwise this is a misc instruction.
	// If the 4th bit is a 0 we have a short instruction, else a long one.
	if (((instruction>>12)&1)==0) {
		// Short misc instruction
		info->type=BytecodeInstructionTypeMisc;
		info->length=BytecodeInstructionLengthShort;

		switch((instruction>>8)&0xF) {
			case 0: info->d.misc.type=BytecodeInstructionMiscTypeNop; return true; break;
			case 1: info->d.misc.type=BytecodeInstructionMiscTypeSyscall; return true; break;
		}

		return false;
	} else {
		// Long misc instruction
		info->type=BytecodeInstructionTypeMisc;
		info->length=BytecodeInstructionLengthLong;

		if ((instruction>>11)&1) {
			info->d.misc.type=BytecodeInstructionMiscTypeSet;
			info->d.misc.d.set.destReg=((instruction>>8)&0x7);
			info->d.misc.d.set.value=(instruction&0xFF);
			return true;
		}

		return false;
	}
}

BytecodeInstructionShort bytecodeInstructionCreateMemory(BytecodeInstructionMemoryType type, BytecodeRegister destReg, BytecodeRegister srcReg) {
	return ((((uint8_t)type)<<6) | (destReg<<3) | srcReg);
}

BytecodeInstructionLong bytecodeInstructionCreateAlu(BytecodeInstructionAluType type, BytecodeRegister destReg, BytecodeRegister opAReg, BytecodeRegister opBReg) {
	uint8_t upper=(0xE0|(((uint8_t)type)<<1)|(destReg>>2));
	uint8_t lower=(((destReg&3)<<6)|(opAReg<<3)|opBReg);
	return (((uint16_t)upper)<<8)|lower;
}

BytecodeInstructionShort bytecodeInstructionCreateMiscNop(void) {
	return 0xC0;
}

BytecodeInstructionShort bytecodeInstructionCreateMiscSyscall(void) {
	return 0xC1;
}

BytecodeInstructionLong bytecodeInstructionCreateMiscSet(BytecodeRegister destReg, uint8_t value) {
	return (((uint16_t)(0xD8|destReg))<<8)|value;
}
