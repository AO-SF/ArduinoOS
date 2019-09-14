#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdbool.h>
#include <stdint.h>

#define BytecodeMagicByte1 '/'
#define BytecodeMagicByte2 '/'
#define BytecodeMagicByte1AsmInstructionStr "load8 r5 r7"
#define BytecodeMagicByte2AsmInstructionStr "load8 r5 r7"

typedef uint16_t BytecodeWord;
typedef uint32_t BytecodeDoubleWord;

#define BytecodeMemoryTotalSize ((BytecodeDoubleWord)0xFFFFu) // 64kb
#define BytecodeMemoryProgmemAddr ((BytecodeWord)0x0000u) // progmem in lower 32kb
#define BytecodeMemoryProgmemSize ((BytecodeWord)0x8000u)
#define BytecodeMemoryRamAddr ((BytecodeWord)(BytecodeMemoryProgmemAddr+BytecodeMemoryProgmemSize)) // ram in upper 32kb
#define BytecodeMemoryRamSize ((BytecodeWord)(BytecodeMemoryTotalSize-BytecodeMemoryProgmemSize))

#define ByteCodeIllegalInstructionByte 0xC3 // invalid single byte instruction, can be used as a marker by assemblers and such

typedef enum {
	BytecodeSignalIdInterrupt,
	BytecodeSignalIdUser1,
	BytecodeSignalIdUser2,
	BytecodeSignalIdSuicide,
	BytecodeSignalIdNB,
} BytecodeSignalId;

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

#define BytecodeRegisterS BytecodeRegister5
#define BytecodeRegisterSP BytecodeRegister6
#define BytecodeRegisterIP BytecodeRegister7

typedef enum {
	BytecodeSyscallIdExit=(0|0),
	BytecodeSyscallIdGetPid=(0|1),
	BytecodeSyscallIdGetArgC=(0|2),
	BytecodeSyscallIdGetArgVN=(0|3),
	BytecodeSyscallIdFork=(0|4),
	BytecodeSyscallIdExec=(0|5),
	BytecodeSyscallIdWaitPid=(0|6),
	BytecodeSyscallIdGetPidPath=(0|7),
	BytecodeSyscallIdGetPidState=(0|8),
	BytecodeSyscallIdGetAllCpuCounts=(0|9),
	BytecodeSyscallIdKill=(0|10),
	BytecodeSyscallIdGetPidRam=(0|11),
	BytecodeSyscallIdSignal=(0|12),
	BytecodeSyscallIdGetPidFdN=(0|13),
	BytecodeSyscallIdRead=(256|0),
	BytecodeSyscallIdWrite=(256|1),
	BytecodeSyscallIdOpen=(256|2),
	BytecodeSyscallIdClose=(256|3),
	BytecodeSyscallIdDirGetChildN=(256|4),
	BytecodeSyscallIdGetPath=(256|5),
	BytecodeSyscallIdResizeFile=(256|6),
	BytecodeSyscallIdFileGetLen=(256|7),
	BytecodeSyscallIdTryReadByte=(256|8),
	BytecodeSyscallIdIsDir=(256|9),
	BytecodeSyscallIdFileExists=(256|10),
	BytecodeSyscallIdDelete=(256|11),
	BytecodeSyscallIdEnvGetStdinFd=(512|0),
	BytecodeSyscallIdEnvSetStdinFd=(512|1),
	BytecodeSyscallIdEnvGetPwd=(512|2),
	BytecodeSyscallIdEnvSetPwd=(512|3),
	BytecodeSyscallIdEnvGetPath=(512|4),
	BytecodeSyscallIdEnvSetPath=(512|5),
	BytecodeSyscallIdEnvGetStdoutFd=(512|6),
	BytecodeSyscallIdEnvSetStdoutFd=(512|7),
	BytecodeSyscallIdTimeMonotonic=(768|0),
	BytecodeSyscallIdRegisterSignalHandler=(1024|0),
	BytecodeSyscallIdShutdown=(1280|0),
	BytecodeSyscallIdMount=(1280|1),
	BytecodeSyscallIdUnmount=(1280|2),
	BytecodeSyscallIdIoctl=(1280|3),
	BytecodeSyscallIdGetLogLevel=(1280|4),
	BytecodeSyscallIdSetLogLevel=(1280|5),
	BytecodeSyscallIdStrchr=(1536|0),
	BytecodeSyscallIdStrchrnul=(1536|1),
	BytecodeSyscallIdMemmove=(1536|2),
	BytecodeSyscallIdHwDeviceRegister=(1792|0),
	BytecodeSyscallIdHwDeviceDeregister=(1792|1),
	BytecodeSyscallIdHwDeviceGetType=(1792|2),
	BytecodeSyscallIdHwDeviceSdCardReaderMount=(1792|3),
	BytecodeSyscallIdHwDeviceSdCardReaderUnmount=(1792|4),
} BytecodeSyscallId;

typedef enum {
	BytecodeInstructionTypeMemory,
	BytecodeInstructionTypeAlu,
	BytecodeInstructionTypeMisc,
} BytecodeInstructionType;

typedef enum {
	BytecodeInstructionLength1Byte=1,
	BytecodeInstructionLength2Byte=2,
	BytecodeInstructionLength3Byte=3,
} BytecodeInstructionLength;

typedef uint8_t BytecodeInstruction1Byte;
typedef BytecodeWord BytecodeInstruction2Byte;
typedef uint8_t BytecodeInstruction3Byte[3];

typedef enum {
	BytecodeInstructionMemoryTypeLoad8,
	BytecodeInstructionMemoryTypeStore8,
	BytecodeInstructionMemoryTypeSet4,
} BytecodeInstructionMemoryType;

typedef struct {
	BytecodeInstructionMemoryType type;
	BytecodeRegister destReg, srcReg;
	BytecodeRegister set4Value;
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
	BytecodeInstructionAluExtraTypeNot,
	BytecodeInstructionAluExtraTypeStore16,
	BytecodeInstructionAluExtraTypeLoad16,
	BytecodeInstructionAluExtraTypePush16,
	BytecodeInstructionAluExtraTypePop16,
	BytecodeInstructionAluExtraTypeCall,
	BytecodeInstructionAluExtraTypeXchg8,
} BytecodeInstructionAluExtraType;

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
	BytecodeInstructionAluTypeCmp,
	BytecodeInstructionAluTypeShiftLeft,
	BytecodeInstructionAluTypeShiftRight,
	BytecodeInstructionAluTypeSkip,
	BytecodeInstructionAluTypeExtra,
} BytecodeInstructionAluType;

typedef struct {
	BytecodeInstructionAluType type;
	BytecodeRegister destReg, opAReg, opBReg;
	uint8_t incDecValue; // post adjustment (i.e. true value)
} BytecodeInstructionAluInfo;

typedef enum {
	BytecodeSyscallIdIoctlCommandDevTtyS0SetEcho,
	BytecodeSyscallIdIoctlCommandDevPinSetMode,
} BytecodeSyscallIdIoctlCommand;

typedef enum {
	BytecodeInstructionMiscTypeNop,
	BytecodeInstructionMiscTypeSyscall,
	BytecodeInstructionMiscTypeClearInstructionCache,
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
	union {
		BytecodeInstructionMemoryInfo memory;
		BytecodeInstructionAluInfo alu;
		BytecodeInstructionMiscInfo misc;
	} d;
} BytecodeInstructionInfo;

BytecodeInstructionLength bytecodeInstructionParseLength(BytecodeInstruction3Byte instruction); // Returns instruction's length by looking at the upper bits (without fully verifying the instruction is valid)
void bytecodeInstructionParse(BytecodeInstructionInfo *info, BytecodeInstruction3Byte instruction);

BytecodeInstruction1Byte bytecodeInstructionCreateMemory(BytecodeInstructionMemoryType type, BytecodeRegister destReg, BytecodeRegister srcReg); // for Xchg8 the addr is put in destReg, then the src/dest reg is put into srcReg
BytecodeInstruction1Byte bytecodeInstructionCreateMemorySet4(BytecodeRegister destReg, uint8_t value); // destReg<4, value<16
BytecodeInstruction2Byte bytecodeInstructionCreateAlu(BytecodeInstructionAluType type, BytecodeRegister destReg, BytecodeRegister opAReg, BytecodeRegister opBReg);
BytecodeInstruction2Byte bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluType type, BytecodeRegister destReg, uint8_t incDecValue);
BytecodeInstruction1Byte bytecodeInstructionCreateMiscNop(void);
BytecodeInstruction1Byte bytecodeInstructionCreateMiscSyscall(void);
BytecodeInstruction1Byte bytecodeInstructionCreateMiscClearInstructionCache(void);
BytecodeInstruction2Byte bytecodeInstructionCreateMiscSet8(BytecodeRegister destReg, uint8_t value);
void bytecodeInstructionCreateMiscSet16(BytecodeInstruction3Byte instruction, BytecodeRegister destReg, uint16_t value);

// Generic set instruction, converting to either set4, set8 or set16 as required.
BytecodeInstructionLength bytecodeInstructionCreateSet(BytecodeInstruction3Byte instruction, BytecodeRegister destReg, uint16_t value); // returns length of instruction chosen

#endif
