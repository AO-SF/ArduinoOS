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
#define M(a,b) (((a)*256)|(b))
	BytecodeSyscallIdExit=M(0,0),
	BytecodeSyscallIdGetPid=M(0,1),
	BytecodeSyscallIdGetArgC=M(0,2),
	BytecodeSyscallIdGetArgVN=M(0,3),
	BytecodeSyscallIdFork=M(0,4),
	BytecodeSyscallIdExec=M(0,5),
	BytecodeSyscallIdWaitPid=M(0,6),
	BytecodeSyscallIdGetPidPath=M(0,7),
	BytecodeSyscallIdGetPidState=M(0,8),
	BytecodeSyscallIdGetAllCpuCounts=M(0,9),
	BytecodeSyscallIdKill=M(0,10),
	BytecodeSyscallIdGetPidRam=M(0,11),
	BytecodeSyscallIdSignal=M(0,12),
	BytecodeSyscallIdGetPidFdN=M(0,13),
	BytecodeSyscallIdExec2=M(0,14),
	BytecodeSyscallIdRead=M(1,0),
	BytecodeSyscallIdWrite=M(1,1),
	BytecodeSyscallIdOpen=M(1,2),
	BytecodeSyscallIdClose=M(1,3),
	BytecodeSyscallIdDirGetChildN=M(1,4),
	BytecodeSyscallIdGetPath=M(1,5),
	BytecodeSyscallIdResizeFile=M(1,6),
	BytecodeSyscallIdGetFileLen=M(1,7),
	BytecodeSyscallIdTryReadByte=M(1,8),
	BytecodeSyscallIdIsDir=M(1,9),
	BytecodeSyscallIdFileExists=M(1,10),
	BytecodeSyscallIdDelete=M(1,11),
	BytecodeSyscallIdRead32=M(1,12),
	BytecodeSyscallIdWrite32=M(1,13),
	BytecodeSyscallIdResizeFile32=M(1,14),
	BytecodeSyscallIdGetFileLen32=M(1,15),
	BytecodeSyscallIdEnvGetStdinFd=M(2,0),
	BytecodeSyscallIdEnvSetStdinFd=M(2,1),
	BytecodeSyscallIdEnvGetPwd=M(2,2),
	BytecodeSyscallIdEnvSetPwd=M(2,3),
	BytecodeSyscallIdEnvGetPath=M(2,4),
	BytecodeSyscallIdEnvSetPath=M(2,5),
	BytecodeSyscallIdEnvGetStdoutFd=M(2,6),
	BytecodeSyscallIdEnvSetStdoutFd=M(2,7),
	BytecodeSyscallIdTimeMonotonic=M(3,0),
	BytecodeSyscallIdRegisterSignalHandler=M(4,0),
	BytecodeSyscallIdShutdown=M(5,0),
	BytecodeSyscallIdMount=M(5,1),
	BytecodeSyscallIdUnmount=M(5,2),
	BytecodeSyscallIdIoctl=M(5,3),
	BytecodeSyscallIdGetLogLevel=M(5,4),
	BytecodeSyscallIdSetLogLevel=M(5,5),
	BytecodeSyscallIdStrchr=M(6,0),
	BytecodeSyscallIdStrchrnul=M(6,1),
	BytecodeSyscallIdMemmove=M(6,2),
	ByteCodeSyscallIdMemcmp=M(6,3),
	ByteCodeSyscallIdStrrchr=M(6,4),
	ByteCodeSyscallIdStrcmp=M(6,5),
	BytecodeSyscallIdHwDeviceRegister=M(7,0),
	BytecodeSyscallIdHwDeviceDeregister=M(7,1),
	BytecodeSyscallIdHwDeviceGetType=M(7,2),
	BytecodeSyscallIdHwDeviceSdCardReaderMount=M(7,3),
	BytecodeSyscallIdHwDeviceSdCardReaderUnmount=M(7,4),
	BytecodeSyscallIdHwDeviceDht22GetTemperature=M(7,5),
	BytecodeSyscallIdHwDeviceDht22GetHumidity=M(7,6),
#undef M
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
	BytecodeInstructionAluExtraTypeClz,
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
	BytecodeInstructionMiscTypeIllegal,
	BytecodeInstructionMiscTypeDebug,
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
BytecodeInstruction1Byte bytecodeInstructionCreateMiscIllegal(void);
BytecodeInstruction1Byte bytecodeInstructionCreateMiscDebug(void);
BytecodeInstruction2Byte bytecodeInstructionCreateMiscSet8(BytecodeRegister destReg, uint8_t value);
void bytecodeInstructionCreateMiscSet16(BytecodeInstruction3Byte instruction, BytecodeRegister destReg, uint16_t value);

// Generic set instruction, converting to either set4, set8 or set16 as required.
BytecodeInstructionLength bytecodeInstructionCreateSet(BytecodeInstruction3Byte instruction, BytecodeRegister destReg, uint16_t value); // returns length of instruction chosen

#endif
