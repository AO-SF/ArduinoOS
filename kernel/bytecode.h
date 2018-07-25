#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t ByteCodeWord;

#define ByteCodeMemoryTotalSize ((ByteCodeWord)0xFFFF) // 64kb
#define ByteCodeMemoryProgmemAddr ((ByteCodeWord)0x0000) // progmem in lower 32kb
#define ByteCodeMemoryProgmemSize ((ByteCodeWord)0x8000)
#define ByteCodeMemoryRamAddr (ByteCodeMemoryProgmemAddr+ByteCodeMemoryProgmemSize) // ram in upper 32kb
#define ByteCodeMemoryRamSize (ByteCodeMemoryTotalSize-ByteCodeMemoryProgmemSize)

typedef enum {
	BytecodeRegister0,
	BytecodeRegister1,
	BytecodeRegister2,
	BytecodeRegister3,
	BytecodeRegister4,
	BytecodeRegister5,
	BytecodeRegister6,
	BytecodeRegister7,
	BytecodeRegisterNB,
} BytecodeRegister;

#define ByteCodeRegisterS BytecodeRegister5
#define ByteCodeRegisterSP BytecodeRegister6
#define ByteCodeRegisterIP BytecodeRegister7

typedef enum {
	BytecodeInstructionTypeMemory,
	BytecodeInstructionTypeAlu,
	BytecodeInstructionTypeMisc,
} BytecodeInstructionType;

typedef enum {
	BytecodeInstructionLengthShort,
	BytecodeInstructionLengthStandard,
	BytecodeInstructionLengthLong,
} BytecodeInstructionLength;

typedef uint8_t BytecodeInstructionShort;
typedef ByteCodeWord BytecodeInstructionStandard;
typedef uint8_t BytecodeInstructionLong[3];

typedef enum {
	BytecodeInstructionMemoryTypeStore,
	BytecodeInstructionMemoryTypeLoad,
	BytecodeInstructionMemoryTypeReserved,
} BytecodeInstructionMemoryType;

typedef struct {
	BytecodeInstructionMemoryType type;
	BytecodeRegister destReg, srcReg;
} BytecodeInstructionMemoryInfo;

typedef enum {
	BytecodeInstructionAluCmpBitEqual,
	BytecodeInstructionAluCmpBitEqualZero, // only uses opA
	BytecodeInstructionAluCmpBitNotEqual,
	BytecodeInstructionAluCmpBitNotEqualZero, // only uses opA
	BytecodeInstructionAluCmpBitLessThan,
	BytecodeInstructionAluCmpBitLessEqual,
	BytecodeInstructionAluCmpBitGreaterThan,
	BytecodeInstructionAluCmpBitGreaterEqual,
	BytecodeInstructionAluCmpBitNB,
} BytecodeInstructionAluCmpBit;

extern const char *byteCodeInstructionAluCmpBitStrings[BytecodeInstructionAluCmpBitNB];

typedef enum {
	BytecodeInstructionAluCmpMaskEqual=(1u<<BytecodeInstructionAluCmpBitEqual),
	BytecodeInstructionAluCmpMaskEqualZero=(1u<<BytecodeInstructionAluCmpBitEqualZero),
	BytecodeInstructionAluCmpMaskNotEqual=(1u<<BytecodeInstructionAluCmpBitNotEqual),
	BytecodeInstructionAluCmpMaskNotEqualZero=(1u<<BytecodeInstructionAluCmpBitNotEqualZero),
	BytecodeInstructionAluCmpMaskLessThan=(1u<<BytecodeInstructionAluCmpBitLessThan),
	BytecodeInstructionAluCmpMaskLessEqual=(1u<<BytecodeInstructionAluCmpBitLessEqual),
	BytecodeInstructionAluCmpMaskGreaterThan=(1u<<BytecodeInstructionAluCmpBitGreaterThan),
	BytecodeInstructionAluCmpMaskGreaterEqual=(1u<<BytecodeInstructionAluCmpBitGreaterEqual),
	BytecodeInstructionAluCmpMaskNB=(1u<<BytecodeInstructionAluCmpBitNB),
} BytecodeInstructionAluCmpMask;

typedef enum {
	BytecodeInstructionAluTypeInc,
	BytecodeInstructionAluTypeDec,
	BytecodeInstructionAluTypeAdd,
	BytecodeInstructionAluTypeSub,
	BytecodeInstructionAluTypeMul,
	BytecodeInstructionAluTypeDiv,
	BytecodeInstructionAluTypeXor,
	BytecodeInstructionAluTypeOr,
	BytecodeInstructionAluTypeAnd,
	BytecodeInstructionAluTypeNot,
	BytecodeInstructionAluTypeCmp,
	BytecodeInstructionAluTypeShiftLeft,
	BytecodeInstructionAluTypeShiftRight,
	BytecodeInstructionAluTypeSkip,
	BytecodeInstructionAluTypeStore16,
	BytecodeInstructionAluTypeLoad16,
} BytecodeInstructionAluType;

typedef struct {
	BytecodeInstructionAluType type;
	BytecodeRegister destReg, opAReg, opBReg;
	uint8_t incDecValue; // post adjustment (i.e. true value)
} BytecodeInstructionAluInfo;

typedef enum {
	ByteCodeSyscallIdExit=(0|0),
	ByteCodeSyscallIdGetPid=(0|1),
	ByteCodeSyscallIdGetArgC=(0|2),
	ByteCodeSyscallIdGetArgVN=(0|3),
	ByteCodeSyscallIdRead=(256|0),
	ByteCodeSyscallIdWrite=(256|1),
	ByteCodeSyscallIdOpen=(256|2),
} ByteCodeSyscallId;

typedef enum {
	ByteCodeFdStdin=0,
	ByteCodeFdStdout=1,
} ByteCodeFd;

typedef enum {
	BytecodeInstructionMiscTypeNop,
	BytecodeInstructionMiscTypeSyscall,
	BytecodeInstructionMiscTypeSet8,
	BytecodeInstructionMiscTypeSet16,
} BytecodeInstructionMiscType;

typedef struct {
	BytecodeRegister destReg;
	uint8_t value;
} BytecodeInstructionMiscSet8Info;

typedef struct {
	BytecodeRegister destReg;
	uint16_t value;
} BytecodeInstructionMiscSet16Info;

typedef struct {
	BytecodeInstructionMiscType type;
	union {
		BytecodeInstructionMiscSet8Info set8;
		BytecodeInstructionMiscSet16Info set16;
	} d;
} BytecodeInstructionMiscInfo;

typedef struct {
	BytecodeInstructionType type;
	BytecodeInstructionLength length;
	union {
		BytecodeInstructionMemoryInfo memory;
		BytecodeInstructionAluInfo alu;
		BytecodeInstructionMiscInfo misc;
	} d;
} BytecodeInstructionInfo;

BytecodeInstructionLength bytecodeInstructionParseLength(BytecodeInstructionLong instruction); // Returns instruction's length by looking at the upper bits (without fully verifying the instruction is valid)
bool bytecodeInstructionParse(BytecodeInstructionInfo *info, BytecodeInstructionLong instruction);

BytecodeInstructionShort bytecodeInstructionCreateMemory(BytecodeInstructionMemoryType type, BytecodeRegister destReg, BytecodeRegister srcReg);
BytecodeInstructionStandard bytecodeInstructionCreateAlu(BytecodeInstructionAluType type, BytecodeRegister destReg, BytecodeRegister opAReg, BytecodeRegister opBReg);
BytecodeInstructionStandard bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluType type, BytecodeRegister destReg, uint8_t incDecValue);
BytecodeInstructionShort bytecodeInstructionCreateMiscNop(void);
BytecodeInstructionShort bytecodeInstructionCreateMiscSyscall(void);
BytecodeInstructionStandard bytecodeInstructionCreateMiscSet8(BytecodeRegister destReg, uint8_t value);
void bytecodeInstructionCreateMiscSet16(BytecodeInstructionLong instruction, BytecodeRegister destReg, uint16_t value);

#endif
