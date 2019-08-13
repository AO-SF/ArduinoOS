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

BytecodeInstructionLength bytecodeInstructionParseLength(BytecodeInstruction3Byte instruction) {
	if (instruction[0]<0xD0)
		return BytecodeInstructionLength1Byte;
	else if ((instruction[0]>>3)!=0x1B)
		return BytecodeInstructionLength2Byte;
	else
		return BytecodeInstructionLength3Byte;
}

void bytecodeInstructionParse(BytecodeInstructionInfo *info, BytecodeInstruction3Byte instruction) {
	// Parse instruction
	uint8_t upperTwoBits=(instruction[0]>>6);
	if (upperTwoBits!=0x3) {
		// If first two bits are not both 1 then we are dealing with a memory-type instruction.
		info->type=BytecodeInstructionTypeMemory;
		info->d.memory.type=upperTwoBits;
		if (info->d.memory.type==BytecodeInstructionMemoryTypeSet4) {
			info->d.memory.destReg=((instruction[0]>>4)&0x3);
			info->d.memory.set4Value=(instruction[0]&0xF);
		} else {
			info->d.memory.destReg=((instruction[0]>>3)&0x7);
			info->d.memory.srcReg=(instruction[0]&0x7);
		}
	} else if (instruction[0] & 0x20) {
		// Otherwise if the 3rd bit is also 1 then this is an ALU instruction
		uint16_t upper16=(((uint16_t)instruction[0])<<8)|instruction[1];
		info->type=BytecodeInstructionTypeAlu;
		info->d.alu.type=((upper16>>9)&0xF);
		info->d.alu.destReg=((upper16>>6)&0x7);
		info->d.alu.opAReg=((upper16>>3)&0x7);
		info->d.alu.opBReg=(upper16&0x7);
		info->d.alu.incDecValue=(upper16&63)+1;
	} else {
		// Otherwise this is a misc instruction.
		info->type=BytecodeInstructionTypeMisc;
		if (instruction[0] & 0x10) {
			// If the 4th bit is a 1 we have a set8/16 instruction
			if (instruction[0] & 0x8) {
				info->d.misc.type=BytecodeInstructionMiscTypeSet16;
				info->d.misc.d.set16.destReg=(instruction[0]&0x7);
				info->d.misc.d.set16.value=((uint16_t)(instruction[1])<<8)|instruction[2];
			} else {
				info->d.misc.type=BytecodeInstructionMiscTypeSet8;
				info->d.misc.d.set8.destReg=(instruction[0]&0x7);
				info->d.misc.d.set8.value=instruction[1];
			}
		} else
			// Otherwise we have a short misc instruction
			info->d.misc.type=BytecodeInstructionMiscTypeNop+(instruction[0] & 0xF);
	}
}

BytecodeInstruction1Byte bytecodeInstructionCreateMemory(BytecodeInstructionMemoryType type, BytecodeRegister destReg, BytecodeRegister srcReg) {
	return ((((uint8_t)type)<<6) | (destReg<<3) | srcReg);
}

BytecodeInstruction1Byte bytecodeInstructionCreateMemorySet4(BytecodeRegister destReg, uint8_t value) {
	assert(destReg<4);
	assert(value<16);

	return ((((uint8_t)BytecodeInstructionMemoryTypeSet4)<<6) | (destReg<<4) | value);
}

BytecodeInstruction2Byte bytecodeInstructionCreateAlu(BytecodeInstructionAluType type, BytecodeRegister destReg, BytecodeRegister opAReg, BytecodeRegister opBReg) {
	assert(destReg<BytecodeRegisterNB);
	assert(opAReg<BytecodeRegisterNB);
	assert(opBReg<BytecodeRegisterNB);

	uint8_t upper=(((uint8_t)0xE0)|(((uint8_t)type)<<1)|(((uint8_t)destReg)>>2));
	uint8_t lower=(((((uint8_t)destReg)&3)<<6)|(((uint8_t)opAReg)<<3)|opBReg);

	return (((uint16_t)upper)<<8)|lower;
}

BytecodeInstruction2Byte bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluType type, BytecodeRegister destReg, uint8_t incDecValue) {
	assert(type==BytecodeInstructionAluTypeInc || type==BytecodeInstructionAluTypeDec);
	assert(incDecValue>0 && incDecValue<64);

	BytecodeRegister opAReg=(incDecValue-1)>>3;
	BytecodeRegister opBReg=(incDecValue-1)&7;

	return bytecodeInstructionCreateAlu(type, destReg, opAReg, opBReg);
}

BytecodeInstruction1Byte bytecodeInstructionCreateMiscNop(void) {
	return 0xC0;
}

BytecodeInstruction1Byte bytecodeInstructionCreateMiscSyscall(void) {
	return 0xC1;
}

BytecodeInstruction1Byte bytecodeInstructionCreateMiscClearInstructionCache(void) {
	return 0xC2;
}

BytecodeInstruction2Byte bytecodeInstructionCreateMiscSet8(BytecodeRegister destReg, uint8_t value) {
	return (((uint16_t)(0xD0|destReg))<<8)|value;
}

void bytecodeInstructionCreateMiscSet16(BytecodeInstruction3Byte instruction, BytecodeRegister destReg, uint16_t value) {
	instruction[0]=(0xD8|destReg);
	instruction[1]=(value>>8);
	instruction[2]=(value&0xFF);
}

void bytecodeInstructionCreateSet(BytecodeInstruction3Byte instruction, BytecodeRegister destReg, uint16_t value) {
	if (value<16 && destReg<4)
		instruction[0]=bytecodeInstructionCreateMemorySet4(destReg, value);
	else if (value<256) {
		BytecodeInstruction2Byte op=bytecodeInstructionCreateMiscSet8(destReg, value);
		instruction[0]=(op>>8);
		instruction[1]=(op&0xFF);
	} else
		bytecodeInstructionCreateMiscSet16(instruction, destReg, value);
}
