#include <assert.h>

#include "bytecode.h"

#ifndef ARDUINO
const char *byteCodeInstructionAluCmpBitStrings[BytecodeInstructionAluCmpBitNB]={
	[BytecodeInstructionAluCmpBitEqual]="Equal",
	[BytecodeInstructionAluCmpBitEqualZero]="EqualZero",
	[BytecodeInstructionAluCmpBitNotEqual]="NotEqual",
	[BytecodeInstructionAluCmpBitNotEqualZero]="NotEqualZero",
	[BytecodeInstructionAluCmpBitLessThan]="LessThan",
	[BytecodeInstructionAluCmpBitLessEqual]="LessEqual",
	[BytecodeInstructionAluCmpBitGreaterThan]="GreaterThan",
	[BytecodeInstructionAluCmpBitGreaterEqual]="GreaterEqual",
};
#endif

BytecodeInstructionLength bytecodeInstructionParseLength(BytecodeInstructionLong instruction) {
	if (instruction[0]<0xD0)
		return BytecodeInstructionLengthShort;
	else if ((instruction[0]>>3)!=0x1B)
		return BytecodeInstructionLengthStandard;
	else
		return BytecodeInstructionLengthLong;
}

bool bytecodeInstructionParse(BytecodeInstructionInfo *info, BytecodeInstructionLong instruction) {

	// If first two bits are not both 1 then we are dealing with a memory-type instruction.
	uint8_t upperTwoBits=(instruction[0]>>6);
	if (upperTwoBits!=0x3) {
		info->type=BytecodeInstructionTypeMemory;
		info->d.memory.type=upperTwoBits;
		info->d.memory.destReg=((instruction[0]>>3)&0x7);
		info->d.memory.srcReg=(instruction[0]&0x7);
		return true;
	}

	// Otherwise if the 3rd bit is also 1 then this is an ALU instruction
	if (((instruction[0]>>5)&1)==1) {
		info->type=BytecodeInstructionTypeAlu;

		uint16_t upper16=(((uint16_t)instruction[0])<<8)|instruction[1];
		info->d.alu.type=((upper16>>9)&0xF);
		info->d.alu.destReg=((upper16>>6)&0x7);
		info->d.alu.opAReg=((upper16>>3)&0x7);
		info->d.alu.opBReg=(upper16&0x7);
		info->d.alu.incDecValue=(upper16&63)+1;

		return true;
	}

	// Otherwise this is a misc instruction.
	// If the 4th bit is a 0 we have a short instruction, else a long one.
	if (((instruction[0]>>4)&1)==0) {
		// Short misc instruction
		info->type=BytecodeInstructionTypeMisc;

		switch(instruction[0]&0xF) {
			case 0: info->d.misc.type=BytecodeInstructionMiscTypeNop; return true; break;
			case 1: info->d.misc.type=BytecodeInstructionMiscTypeSyscall; return true; break;
			case 2: info->d.misc.type=BytecodeInstructionMiscTypeClearInstructionCache; return true; break;
		}

		return false;
	} else {
		// Long misc instruction
		info->type=BytecodeInstructionTypeMisc;

		if ((instruction[0]>>3)&1) {
			info->d.misc.type=BytecodeInstructionMiscTypeSet16;
			info->d.misc.d.set16.destReg=(instruction[0]&0x7);
			info->d.misc.d.set16.value=((uint16_t)(instruction[1])<<8)|instruction[2];
		} else {
			info->d.misc.type=BytecodeInstructionMiscTypeSet8;
			info->d.misc.d.set8.destReg=(instruction[0]&0x7);
			info->d.misc.d.set8.value=instruction[1];
		}

		return true;
	}
}

BytecodeInstructionShort bytecodeInstructionCreateMemory(BytecodeInstructionMemoryType type, BytecodeRegister destReg, BytecodeRegister srcReg) {
	return ((((uint8_t)type)<<6) | (destReg<<3) | srcReg);
}

BytecodeInstructionStandard bytecodeInstructionCreateAlu(BytecodeInstructionAluType type, BytecodeRegister destReg, BytecodeRegister opAReg, BytecodeRegister opBReg) {
	assert(destReg<BytecodeRegisterNB);
	assert(opAReg<BytecodeRegisterNB);
	assert(opBReg<BytecodeRegisterNB);

	uint8_t upper=(((uint8_t)0xE0)|(((uint8_t)type)<<1)|(((uint8_t)destReg)>>2));
	uint8_t lower=(((((uint8_t)destReg)&3)<<6)|(((uint8_t)opAReg)<<3)|opBReg);

	return (((uint16_t)upper)<<8)|lower;
}

BytecodeInstructionStandard bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluType type, BytecodeRegister destReg, uint8_t incDecValue) {
	assert(type==BytecodeInstructionAluTypeInc || type==BytecodeInstructionAluTypeDec);
	assert(incDecValue>0 && incDecValue<64);

	BytecodeRegister opAReg=(incDecValue-1)>>3;
	BytecodeRegister opBReg=(incDecValue-1)&7;

	return bytecodeInstructionCreateAlu(type, destReg, opAReg, opBReg);
}

BytecodeInstructionShort bytecodeInstructionCreateMiscNop(void) {
	return 0xC0;
}

BytecodeInstructionShort bytecodeInstructionCreateMiscSyscall(void) {
	return 0xC1;
}

BytecodeInstructionShort bytecodeInstructionCreateMiscClearInstructionCache(void) {
	return 0xC2;
}

BytecodeInstructionStandard bytecodeInstructionCreateMiscSet8(BytecodeRegister destReg, uint8_t value) {
	return (((uint16_t)(0xD0|destReg))<<8)|value;
}

void bytecodeInstructionCreateMiscSet16(BytecodeInstructionLong instruction, BytecodeRegister destReg, uint16_t value) {
	instruction[0]=(0xD8|destReg);
	instruction[1]=(value>>8);
	instruction[2]=(value&0xFF);
}
