#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t ByteCodeWord;
typedef uint32_t ByteCodeDoubleWord;

#define ByteCodeMemoryTotalSize ((ByteCodeDoubleWord)0xFFFFu) // 64kb
#define ByteCodeMemoryProgmemAddr ((ByteCodeWord)0x0000u) // progmem in lower 32kb
#define ByteCodeMemoryProgmemSize ((ByteCodeWord)0x8000u)
#define ByteCodeMemoryRamAddr ((ByteCodeWord)(ByteCodeMemoryProgmemAddr+ByteCodeMemoryProgmemSize)) // ram in upper 32kb
#define ByteCodeMemoryRamSize ((ByteCodeWord)(ByteCodeMemoryTotalSize-ByteCodeMemoryProgmemSize))

typedef enum {
	ByteCodeSignalIdInterrupt,
	ByteCodeSignalIdUser1,
	ByteCodeSignalIdUser2,
	ByteCodeSignalIdSuicide,
	ByteCodeSignalIdNB,
} ByteCodeSignalId;

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
	BytecodeInstructionMemoryTypeStore8,
	BytecodeInstructionMemoryTypeLoad8,
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

#ifndef ARDUINO
extern const char *byteCodeInstructionAluCmpBitStrings[BytecodeInstructionAluCmpBitNB];
#endif

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
	ByteCodeSyscallIdIoctlCommandSetEcho,
} ByteCodeSyscallIdIoctlCommand;

typedef enum {
	ByteCodeSyscallIdExit=(0|0),
	ByteCodeSyscallIdGetPid=(0|1),
	ByteCodeSyscallIdGetArgC=(0|2),
	ByteCodeSyscallIdGetArgVN=(0|3),
	ByteCodeSyscallIdFork=(0|4),
	ByteCodeSyscallIdExec=(0|5),
	ByteCodeSyscallIdWaitPid=(0|6),
	ByteCodeSyscallIdGetPidPath=(0|7),
	ByteCodeSyscallIdGetPidState=(0|8),
	ByteCodeSyscallIdGetAllCpuCounts=(0|9),
	ByteCodeSyscallIdKill=(0|10),
	ByteCodeSyscallIdGetPidRam=(0|11),
	ByteCodeSyscallIdSignal=(0|12),
	ByteCodeSyscallIdRead=(256|0),
	ByteCodeSyscallIdWrite=(256|1),
	ByteCodeSyscallIdOpen=(256|2),
	ByteCodeSyscallIdClose=(256|3),
	ByteCodeSyscallIdDirGetChildN=(256|4),
	ByteCodeSyscallIdGetPath=(256|5),
	ByteCodeSyscallIdResizeFile=(256|6),
	ByteCodeSyscallIdFileGetLen=(256|7),
	ByteCodeSyscallIdTryReadByte=(256|8),
	ByteCodeSyscallIdIsDir=(256|9),
	ByteCodeSyscallIdFileExists=(256|10),
	ByteCodeSyscallIdDelete=(256|11),
	ByteCodeSyscallIdEnvGetStdinFd=(512|0),
	ByteCodeSyscallIdEnvSetStdinFd=(512|1),
	ByteCodeSyscallIdEnvGetPwd=(512|2),
	ByteCodeSyscallIdEnvSetPwd=(512|3),
	ByteCodeSyscallIdEnvGetPath=(512|4),
	ByteCodeSyscallIdEnvSetPath=(512|5),
	ByteCodeSyscallIdEnvGetStdoutFd=(512|6),
	ByteCodeSyscallIdEnvSetStdoutFd=(512|7),
	ByteCodeSyscallIdTimeMonotonic=(768|0),
	ByteCodeSyscallIdRegisterSignalHandler=(1024|0),
	ByteCodeSyscallIdShutdown=(1280|0),
	ByteCodeSyscallIdMount=(1280|1),
	ByteCodeSyscallIdUnmount=(1280|2),
	ByteCodeSyscallIdIoctl=(1280|3),
} ByteCodeSyscallId;

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
