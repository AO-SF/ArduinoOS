#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bytecode.h"
#include "util.h"

#define AssemblerLinesMax 65536

typedef struct {
	BytecodeInstructionAluType type;
	const char *str;
	unsigned ops;
	unsigned skipBit;
	unsigned incDecValue;
	BytecodeInstructionAluExtraType extraType;
} AssemblerInstructionAluData;

const AssemblerInstructionAluData assemblerInstructionAluData[]={
	{.type=BytecodeInstructionAluTypeInc, .str="inc", .ops=0, .incDecValue=1},
	{.type=BytecodeInstructionAluTypeInc, .str="inc2", .ops=0, .incDecValue=2},
	{.type=BytecodeInstructionAluTypeInc, .str="inc3", .ops=0, .incDecValue=3},
	{.type=BytecodeInstructionAluTypeInc, .str="inc4", .ops=0, .incDecValue=4},
	{.type=BytecodeInstructionAluTypeInc, .str="inc5", .ops=0, .incDecValue=5},
	{.type=BytecodeInstructionAluTypeInc, .str="inc6", .ops=0, .incDecValue=6},
	{.type=BytecodeInstructionAluTypeInc, .str="inc7", .ops=0, .incDecValue=7},
	{.type=BytecodeInstructionAluTypeInc, .str="inc8", .ops=0, .incDecValue=8},
	{.type=BytecodeInstructionAluTypeInc, .str="inc9", .ops=0, .incDecValue=9},
	{.type=BytecodeInstructionAluTypeInc, .str="inc10", .ops=0, .incDecValue=10},
	{.type=BytecodeInstructionAluTypeInc, .str="inc11", .ops=0, .incDecValue=11},
	{.type=BytecodeInstructionAluTypeInc, .str="inc12", .ops=0, .incDecValue=12},
	{.type=BytecodeInstructionAluTypeInc, .str="inc13", .ops=0, .incDecValue=13},
	{.type=BytecodeInstructionAluTypeInc, .str="inc14", .ops=0, .incDecValue=14},
	{.type=BytecodeInstructionAluTypeInc, .str="inc15", .ops=0, .incDecValue=15},
	{.type=BytecodeInstructionAluTypeInc, .str="inc16", .ops=0, .incDecValue=16},
	{.type=BytecodeInstructionAluTypeInc, .str="inc17", .ops=0, .incDecValue=17},
	{.type=BytecodeInstructionAluTypeInc, .str="inc18", .ops=0, .incDecValue=18},
	{.type=BytecodeInstructionAluTypeInc, .str="inc19", .ops=0, .incDecValue=19},
	{.type=BytecodeInstructionAluTypeInc, .str="inc20", .ops=0, .incDecValue=20},
	{.type=BytecodeInstructionAluTypeInc, .str="inc21", .ops=0, .incDecValue=21},
	{.type=BytecodeInstructionAluTypeInc, .str="inc22", .ops=0, .incDecValue=22},
	{.type=BytecodeInstructionAluTypeInc, .str="inc23", .ops=0, .incDecValue=23},
	{.type=BytecodeInstructionAluTypeInc, .str="inc24", .ops=0, .incDecValue=24},
	{.type=BytecodeInstructionAluTypeInc, .str="inc25", .ops=0, .incDecValue=25},
	{.type=BytecodeInstructionAluTypeInc, .str="inc26", .ops=0, .incDecValue=26},
	{.type=BytecodeInstructionAluTypeInc, .str="inc27", .ops=0, .incDecValue=27},
	{.type=BytecodeInstructionAluTypeInc, .str="inc28", .ops=0, .incDecValue=28},
	{.type=BytecodeInstructionAluTypeInc, .str="inc29", .ops=0, .incDecValue=29},
	{.type=BytecodeInstructionAluTypeInc, .str="inc30", .ops=0, .incDecValue=30},
	{.type=BytecodeInstructionAluTypeInc, .str="inc31", .ops=0, .incDecValue=31},
	{.type=BytecodeInstructionAluTypeInc, .str="inc32", .ops=0, .incDecValue=32},
	{.type=BytecodeInstructionAluTypeInc, .str="inc33", .ops=0, .incDecValue=33},
	{.type=BytecodeInstructionAluTypeInc, .str="inc34", .ops=0, .incDecValue=34},
	{.type=BytecodeInstructionAluTypeInc, .str="inc35", .ops=0, .incDecValue=35},
	{.type=BytecodeInstructionAluTypeInc, .str="inc36", .ops=0, .incDecValue=36},
	{.type=BytecodeInstructionAluTypeInc, .str="inc37", .ops=0, .incDecValue=37},
	{.type=BytecodeInstructionAluTypeInc, .str="inc38", .ops=0, .incDecValue=38},
	{.type=BytecodeInstructionAluTypeInc, .str="inc39", .ops=0, .incDecValue=39},
	{.type=BytecodeInstructionAluTypeInc, .str="inc40", .ops=0, .incDecValue=40},
	{.type=BytecodeInstructionAluTypeInc, .str="inc41", .ops=0, .incDecValue=41},
	{.type=BytecodeInstructionAluTypeInc, .str="inc42", .ops=0, .incDecValue=42},
	{.type=BytecodeInstructionAluTypeInc, .str="inc43", .ops=0, .incDecValue=43},
	{.type=BytecodeInstructionAluTypeInc, .str="inc44", .ops=0, .incDecValue=44},
	{.type=BytecodeInstructionAluTypeInc, .str="inc45", .ops=0, .incDecValue=45},
	{.type=BytecodeInstructionAluTypeInc, .str="inc46", .ops=0, .incDecValue=46},
	{.type=BytecodeInstructionAluTypeInc, .str="inc47", .ops=0, .incDecValue=47},
	{.type=BytecodeInstructionAluTypeInc, .str="inc48", .ops=0, .incDecValue=48},
	{.type=BytecodeInstructionAluTypeInc, .str="inc49", .ops=0, .incDecValue=49},
	{.type=BytecodeInstructionAluTypeInc, .str="inc50", .ops=0, .incDecValue=50},
	{.type=BytecodeInstructionAluTypeInc, .str="inc51", .ops=0, .incDecValue=51},
	{.type=BytecodeInstructionAluTypeInc, .str="inc52", .ops=0, .incDecValue=52},
	{.type=BytecodeInstructionAluTypeInc, .str="inc53", .ops=0, .incDecValue=53},
	{.type=BytecodeInstructionAluTypeInc, .str="inc54", .ops=0, .incDecValue=54},
	{.type=BytecodeInstructionAluTypeInc, .str="inc55", .ops=0, .incDecValue=55},
	{.type=BytecodeInstructionAluTypeInc, .str="inc56", .ops=0, .incDecValue=56},
	{.type=BytecodeInstructionAluTypeInc, .str="inc57", .ops=0, .incDecValue=57},
	{.type=BytecodeInstructionAluTypeInc, .str="inc58", .ops=0, .incDecValue=58},
	{.type=BytecodeInstructionAluTypeInc, .str="inc59", .ops=0, .incDecValue=59},
	{.type=BytecodeInstructionAluTypeInc, .str="inc60", .ops=0, .incDecValue=60},
	{.type=BytecodeInstructionAluTypeInc, .str="inc61", .ops=0, .incDecValue=61},
	{.type=BytecodeInstructionAluTypeInc, .str="inc62", .ops=0, .incDecValue=62},
	{.type=BytecodeInstructionAluTypeInc, .str="inc63", .ops=0, .incDecValue=63},
	{.type=BytecodeInstructionAluTypeInc, .str="inc64", .ops=0, .incDecValue=64},
	{.type=BytecodeInstructionAluTypeDec, .str="dec", .ops=0, .incDecValue=1},
	{.type=BytecodeInstructionAluTypeDec, .str="dec2", .ops=0, .incDecValue=2},
	{.type=BytecodeInstructionAluTypeDec, .str="dec3", .ops=0, .incDecValue=3},
	{.type=BytecodeInstructionAluTypeDec, .str="dec4", .ops=0, .incDecValue=4},
	{.type=BytecodeInstructionAluTypeDec, .str="dec5", .ops=0, .incDecValue=5},
	{.type=BytecodeInstructionAluTypeDec, .str="dec6", .ops=0, .incDecValue=6},
	{.type=BytecodeInstructionAluTypeDec, .str="dec7", .ops=0, .incDecValue=7},
	{.type=BytecodeInstructionAluTypeDec, .str="dec8", .ops=0, .incDecValue=8},
	{.type=BytecodeInstructionAluTypeDec, .str="dec9", .ops=0, .incDecValue=9},
	{.type=BytecodeInstructionAluTypeDec, .str="dec10", .ops=0, .incDecValue=10},
	{.type=BytecodeInstructionAluTypeDec, .str="dec11", .ops=0, .incDecValue=11},
	{.type=BytecodeInstructionAluTypeDec, .str="dec12", .ops=0, .incDecValue=12},
	{.type=BytecodeInstructionAluTypeDec, .str="dec13", .ops=0, .incDecValue=13},
	{.type=BytecodeInstructionAluTypeDec, .str="dec14", .ops=0, .incDecValue=14},
	{.type=BytecodeInstructionAluTypeDec, .str="dec15", .ops=0, .incDecValue=15},
	{.type=BytecodeInstructionAluTypeDec, .str="dec16", .ops=0, .incDecValue=16},
	{.type=BytecodeInstructionAluTypeDec, .str="dec17", .ops=0, .incDecValue=17},
	{.type=BytecodeInstructionAluTypeDec, .str="dec18", .ops=0, .incDecValue=18},
	{.type=BytecodeInstructionAluTypeDec, .str="dec19", .ops=0, .incDecValue=19},
	{.type=BytecodeInstructionAluTypeDec, .str="dec20", .ops=0, .incDecValue=20},
	{.type=BytecodeInstructionAluTypeDec, .str="dec21", .ops=0, .incDecValue=21},
	{.type=BytecodeInstructionAluTypeDec, .str="dec22", .ops=0, .incDecValue=22},
	{.type=BytecodeInstructionAluTypeDec, .str="dec23", .ops=0, .incDecValue=23},
	{.type=BytecodeInstructionAluTypeDec, .str="dec24", .ops=0, .incDecValue=24},
	{.type=BytecodeInstructionAluTypeDec, .str="dec25", .ops=0, .incDecValue=25},
	{.type=BytecodeInstructionAluTypeDec, .str="dec26", .ops=0, .incDecValue=26},
	{.type=BytecodeInstructionAluTypeDec, .str="dec27", .ops=0, .incDecValue=27},
	{.type=BytecodeInstructionAluTypeDec, .str="dec28", .ops=0, .incDecValue=28},
	{.type=BytecodeInstructionAluTypeDec, .str="dec29", .ops=0, .incDecValue=29},
	{.type=BytecodeInstructionAluTypeDec, .str="dec30", .ops=0, .incDecValue=30},
	{.type=BytecodeInstructionAluTypeDec, .str="dec31", .ops=0, .incDecValue=31},
	{.type=BytecodeInstructionAluTypeDec, .str="dec32", .ops=0, .incDecValue=32},
	{.type=BytecodeInstructionAluTypeDec, .str="dec33", .ops=0, .incDecValue=33},
	{.type=BytecodeInstructionAluTypeDec, .str="dec34", .ops=0, .incDecValue=34},
	{.type=BytecodeInstructionAluTypeDec, .str="dec35", .ops=0, .incDecValue=35},
	{.type=BytecodeInstructionAluTypeDec, .str="dec36", .ops=0, .incDecValue=36},
	{.type=BytecodeInstructionAluTypeDec, .str="dec37", .ops=0, .incDecValue=37},
	{.type=BytecodeInstructionAluTypeDec, .str="dec38", .ops=0, .incDecValue=38},
	{.type=BytecodeInstructionAluTypeDec, .str="dec39", .ops=0, .incDecValue=39},
	{.type=BytecodeInstructionAluTypeDec, .str="dec40", .ops=0, .incDecValue=40},
	{.type=BytecodeInstructionAluTypeDec, .str="dec41", .ops=0, .incDecValue=41},
	{.type=BytecodeInstructionAluTypeDec, .str="dec42", .ops=0, .incDecValue=42},
	{.type=BytecodeInstructionAluTypeDec, .str="dec43", .ops=0, .incDecValue=43},
	{.type=BytecodeInstructionAluTypeDec, .str="dec44", .ops=0, .incDecValue=44},
	{.type=BytecodeInstructionAluTypeDec, .str="dec45", .ops=0, .incDecValue=45},
	{.type=BytecodeInstructionAluTypeDec, .str="dec46", .ops=0, .incDecValue=46},
	{.type=BytecodeInstructionAluTypeDec, .str="dec47", .ops=0, .incDecValue=47},
	{.type=BytecodeInstructionAluTypeDec, .str="dec48", .ops=0, .incDecValue=48},
	{.type=BytecodeInstructionAluTypeDec, .str="dec49", .ops=0, .incDecValue=49},
	{.type=BytecodeInstructionAluTypeDec, .str="dec50", .ops=0, .incDecValue=50},
	{.type=BytecodeInstructionAluTypeDec, .str="dec51", .ops=0, .incDecValue=51},
	{.type=BytecodeInstructionAluTypeDec, .str="dec52", .ops=0, .incDecValue=52},
	{.type=BytecodeInstructionAluTypeDec, .str="dec53", .ops=0, .incDecValue=53},
	{.type=BytecodeInstructionAluTypeDec, .str="dec54", .ops=0, .incDecValue=54},
	{.type=BytecodeInstructionAluTypeDec, .str="dec55", .ops=0, .incDecValue=55},
	{.type=BytecodeInstructionAluTypeDec, .str="dec56", .ops=0, .incDecValue=56},
	{.type=BytecodeInstructionAluTypeDec, .str="dec57", .ops=0, .incDecValue=57},
	{.type=BytecodeInstructionAluTypeDec, .str="dec58", .ops=0, .incDecValue=58},
	{.type=BytecodeInstructionAluTypeDec, .str="dec59", .ops=0, .incDecValue=59},
	{.type=BytecodeInstructionAluTypeDec, .str="dec60", .ops=0, .incDecValue=60},
	{.type=BytecodeInstructionAluTypeDec, .str="dec61", .ops=0, .incDecValue=61},
	{.type=BytecodeInstructionAluTypeDec, .str="dec62", .ops=0, .incDecValue=62},
	{.type=BytecodeInstructionAluTypeDec, .str="dec63", .ops=0, .incDecValue=63},
	{.type=BytecodeInstructionAluTypeDec, .str="dec64", .ops=0, .incDecValue=64},
	{.type=BytecodeInstructionAluTypeAdd, .str="add", .ops=2},
	{.type=BytecodeInstructionAluTypeSub, .str="sub", .ops=2},
	{.type=BytecodeInstructionAluTypeMul, .str="mul", .ops=2},
	{.type=BytecodeInstructionAluTypeDiv, .str="div", .ops=2},
	{.type=BytecodeInstructionAluTypeXor, .str="xor", .ops=2},
	{.type=BytecodeInstructionAluTypeOr, .str="or", .ops=2},
	{.type=BytecodeInstructionAluTypeAnd, .str="and", .ops=2},
	{.type=BytecodeInstructionAluTypeCmp, .str="cmp", .ops=2},
	{.type=BytecodeInstructionAluTypeShiftLeft, .str="shl", .ops=2},
	{.type=BytecodeInstructionAluTypeShiftRight, .str="shr", .ops=2},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipeq", .ops=0, .skipBit=BytecodeInstructionAluCmpBitEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipeqz", .ops=0, .skipBit=BytecodeInstructionAluCmpBitEqualZero},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipneq", .ops=0, .skipBit=BytecodeInstructionAluCmpBitNotEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipneqz", .ops=0, .skipBit=BytecodeInstructionAluCmpBitNotEqualZero},
	{.type=BytecodeInstructionAluTypeSkip, .str="skiplt", .ops=0, .skipBit=BytecodeInstructionAluCmpBitLessThan},
	{.type=BytecodeInstructionAluTypeSkip, .str="skiple", .ops=0, .skipBit=BytecodeInstructionAluCmpBitLessEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipgt", .ops=0, .skipBit=BytecodeInstructionAluCmpBitGreaterThan},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipge", .ops=0, .skipBit=BytecodeInstructionAluCmpBitGreaterEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip0", .ops=0, .skipBit=0},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip1", .ops=0, .skipBit=1},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip2", .ops=0, .skipBit=2},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip3", .ops=0, .skipBit=3},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip4", .ops=0, .skipBit=4},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip5", .ops=0, .skipBit=5},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip6", .ops=0, .skipBit=6},
	{.type=BytecodeInstructionAluTypeSkip, .str="skip7", .ops=0, .skipBit=7},
	{.type=BytecodeInstructionAluTypeExtra, .str="not", .ops=1, .extraType=BytecodeInstructionAluExtraTypeNot},
	{.type=BytecodeInstructionAluTypeExtra, .str="store16", .ops=1, .extraType=BytecodeInstructionAluExtraTypeStore16},
	{.type=BytecodeInstructionAluTypeExtra, .str="load16", .ops=1, .extraType=BytecodeInstructionAluExtraTypeLoad16},
	{.type=BytecodeInstructionAluTypeExtra, .str="push16", .ops=0, .extraType=BytecodeInstructionAluExtraTypePush16},
	{.type=BytecodeInstructionAluTypeExtra, .str="pop16", .ops=0, .extraType=BytecodeInstructionAluExtraTypePop16},
};

typedef enum {
	AssemblerInstructionTypeAllocation,
	AssemblerInstructionTypeDefine,
	AssemblerInstructionTypeMov,
	AssemblerInstructionTypeLabel,
	AssemblerInstructionTypeSyscall,
	AssemblerInstructionTypeClearInstructionCache,
	AssemblerInstructionTypeDebug,
	AssemblerInstructionTypeAlu,
	AssemblerInstructionTypeJmp,
	AssemblerInstructionTypePush8,
	AssemblerInstructionTypePop8,
	AssemblerInstructionTypeCall,
	AssemblerInstructionTypeRet,
	AssemblerInstructionTypeStore8,
	AssemblerInstructionTypeLoad8,
	AssemblerInstructionTypeXchg8,
	AssemblerInstructionTypeConst,
	AssemblerInstructionTypeNop,
} AssemblerInstructionType;

typedef struct {
	uint16_t membSize, len, totalSize; // for membSize: 1=byte, 2=word
	const char *symbol;

	uint16_t ramOffset;
} AssemblerInstructionAllocation;

typedef struct {
	uint16_t membSize, len, totalSize; // for membSize: 1=byte, 2=word
	const char *symbol;
	uint8_t data[1024]; // TODO: this is pretty wasteful...

	uint16_t pointerLineIndex; // pointer to instruction actually containing data. set to self initially and if not pointing into another define's data
	uint16_t pointerOffset; // how far into pointed-to-data is our data?
} AssemblerInstructionDefine;

typedef struct {
	const char *dest;
	char *src;
} AssemblerInstructionMov;

typedef struct {
	const char *symbol;
} AssemblerInstructionLabel;

typedef struct {
	BytecodeInstructionAluType type;
	const char *dest;
	const char *opA;
	const char *opB;
	uint8_t skipBit;
	uint8_t incDecValue;
	uint8_t extraType;
} AssemblerInstructionAlu;

typedef struct {
	const char *addr;
} AssemblerInstructionJmp;

typedef struct {
	const char *src;
} AssemblerInstructionPush8;

typedef struct {
	const char *dest;
} AssemblerInstructionPop8;

typedef struct {
	const char *label;
} AssemblerInstructionCall;

typedef struct {
	const char *dest, *src;
} AssemblerInstructionStore8;

typedef struct {
	const char *dest, *src;
} AssemblerInstructionLoad8;

typedef struct {
	const char *addrReg, *srcDestReg;
} AssemblerInstructionXchg8;

typedef struct {
	const char *symbol;
	BytecodeWord value;
} AssemblerInstructionConst;

#define AssemblerInstructionMachineCodeMax 1024
typedef struct {
	uint16_t lineIndex;
	char *modifiedLineCopy; // so we can have fields pointing into this
	AssemblerInstructionType type;
	union {
		AssemblerInstructionAllocation allocation;
		AssemblerInstructionDefine define;
		AssemblerInstructionMov mov;
		AssemblerInstructionLabel label;
		AssemblerInstructionAlu alu;
		AssemblerInstructionJmp jmp;
		AssemblerInstructionPush8 push8;
		AssemblerInstructionPop8 pop8;
		AssemblerInstructionCall call;
		AssemblerInstructionStore8 store8;
		AssemblerInstructionLoad8 load8;
		AssemblerInstructionXchg8 xchg8;
		AssemblerInstructionConst constSymbol;
	} d;

	uint8_t machineCode[AssemblerInstructionMachineCodeMax]; // TODO: this is pretty wasteful...
	uint16_t machineCodeLen;
	uint16_t machineCodeOffset;
	uint8_t machineCodeInstructions;
} AssemblerInstruction;

typedef struct {
	char *file;
	unsigned lineNum;
	char *original;
	char *modified;
} AssemblerLine;

// TODO: avoid hardcoded limits
#define AssemblerIncludeDirMax 32
#define AssemblerIncludeDirLenMax 1024

typedef struct {
	AssemblerLine *lines[AssemblerLinesMax];
	size_t linesNext;

	AssemblerInstruction instructions[AssemblerLinesMax];
	size_t instructionsNext;

	uint16_t stackRamOffset;

	char includedPaths[256][1024]; // TODO: Avoid hardcoded limits (or at least check them...)
	size_t includePathsNext;

	bool noStack, noScratch;

	char assemblerIncludeDirs[AssemblerIncludeDirMax][AssemblerIncludeDirLenMax];
	size_t assemblerIncludeDirsNext;
} AssemblerProgram;

AssemblerProgram *assemblerProgramNew(void);
void assemblerProgramFree(AssemblerProgram *program);

void assemblerInsertLine(AssemblerProgram *program, AssemblerLine *line, int offset);
bool assemblerInsertLinesFromFile(AssemblerProgram *program, const char *path, int offset);
void assemblerRemoveLine(AssemblerProgram *program, int offset);

bool assemblerProgramPreprocess(AssemblerProgram *program); // strips comments, whitespace etc. returns true if any changes made
bool assemblerProgramLocateInclude(const AssemblerProgram *program, char *destPath, const char *callerPath, const char *srcPath);
bool assemblerProgramHandleNextInclude(AssemblerProgram *program, bool *change); // returns false on failure
bool assemblerProgramHandleNextOption(AssemblerProgram *program, bool *change); // returns false on failure

bool assemblerProgramParseLines(AssemblerProgram *program); // converts lines to initial instructions, returns false on error

bool assemblerProgramShiftDefines(AssemblerProgram *program); // moves defines to the end to avoid getting in the way of code, returns true if any changes made
bool assemblerProgramShrinkDefines(AssemblerProgram *program); // checks if some defines are subsets of others, returns true if any changes made

bool assemblerProgramCalculateInitialMachineCodeLengths(AssemblerProgram *program); // returns false on failure
void assemblerProgramCalculateMachineCodeOffsets(AssemblerProgram *program);
bool assemblerProgramGenerateMachineCode(AssemblerProgram *program, bool *changeFlag); // returns false on failure. if a size reduction is found, the relevant machineCodeLen field is updated and we return immedaitely (without computing the machine code for any remaining instructions)

void assemblerProgramDebugInstructions(const AssemblerProgram *program);

bool assemblerProgramWriteMachineCode(const AssemblerProgram *program, const char *path); // returns false on failure

int assemblerGetAllocationSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetAllocationSymbolAddr(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise result points into RAM memory

int assemblerGetDefineSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetDefineSymbolAddr(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise result points into read-only program memory

int assemblerGetLabelSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetLabelSymbolAddr(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise result points into read-only program memory

int assemblerGetConstSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetConstSymbolValue(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise contains 16 bit value

BytecodeRegister assemblerRegisterFromStr(const char *str); // Returns BytecodeRegisterNB on failure

bool assemblerProgramAddIncludeDir(AssemblerProgram *program, const char *dir);

int main(int argc, char **argv) {
	bool change;

	// Create program struct
	AssemblerProgram *program=assemblerProgramNew();
	if (program==NULL)
		goto done;

	// Parse arguments
	if (argc<3) {
		printf("Usage: %s [--verbose] [-Iincludepath] inputfile outputfile\n", argv[0]);
		goto done;
	}

	bool verbose=false;
	for(int i=1; i<argc-2; ++i) {
		if (strcmp(argv[i], "--verbose")==0)
			verbose=true;
		else if (strncmp(argv[i], "-I", 2)==0) {
			char *spacePtr=strchr(argv[i]+2, ' '); // TODO: Support quotes/escape character or similar to allow spaces in names
			char newDir[AssemblerIncludeDirLenMax];
			if (spacePtr==NULL)
				strcpy(newDir, argv[i]+2);
			else {
				size_t dirLen=spacePtr-(argv[i]+2);
				strncpy(newDir, argv[i]+2, dirLen);
				newDir[dirLen]='\0';
			}
			assemblerProgramAddIncludeDir(program, newDir); // TODO: Check return
		} else
			printf("warning: unknown option '%s'\n", argv[i]);
	}
	const char *inputPath=argv[argc-2];
	const char *outputPath=argv[argc-1];

	// Read input file line-by-line
	char tempPath[1024]={0}; // TODO: better
	if (inputPath[0]!='/') {
		getcwd(tempPath, 1024);
		strcat(tempPath, "/");
	}
	strcat(tempPath, inputPath);
	pathNormalise(tempPath);
	if (!assemblerInsertLinesFromFile(program, tempPath, 0))
		goto done;

	// Preprocess (handle whitespace, includes etc)
	do {
		change=false;

		// Preprocess (strip whitespace, comments, etc)
		while(assemblerProgramPreprocess(program))
			change=true;

		// Handle an include if any
		bool includeChange=false;
		if (!assemblerProgramHandleNextInclude(program, &includeChange))
			goto done;
		change|=includeChange;

		// Handle an option if any (such as nostack or noscratch)
		bool optionChange=false;
		if (!assemblerProgramHandleNextOption(program, &optionChange))
			goto done;
		change|=optionChange;
	} while(change);

	// Prepend initial 'header' bytes/instructions.
	const char *autoFile="<auto>";
	char autoLine[64];
	uint16_t autoLineNext=0;
	AssemblerLine *assemblerLine;

	// Add a couple of lines to put magic bytes at the front of the file
	sprintf(autoLine, "%s ; magic header byte 1", BytecodeMagicByte1AsmInstructionStr);
	assemblerLine=malloc(sizeof(AssemblerLine));
	assemblerLine->lineNum=autoLineNext+1;
	assemblerLine->file=malloc(strlen(autoFile)+1);
	strcpy(assemblerLine->file, autoFile);
	assemblerLine->original=malloc(strlen(autoLine)+1);
	strcpy(assemblerLine->original, autoLine);
	assemblerLine->modified=malloc(strlen(autoLine)+1);
	strcpy(assemblerLine->modified, autoLine);
	assemblerInsertLine(program, assemblerLine, autoLineNext++);

	sprintf(autoLine, "%s ; magic header byte 2", BytecodeMagicByte2AsmInstructionStr);
	assemblerLine=malloc(sizeof(AssemblerLine));
	assemblerLine->lineNum=autoLineNext+1;
	assemblerLine->file=malloc(strlen(autoFile)+1);
	strcpy(assemblerLine->file, autoFile);
	assemblerLine->original=malloc(strlen(autoLine)+1);
	strcpy(assemblerLine->original, autoLine);
	assemblerLine->modified=malloc(strlen(autoLine)+1);
	strcpy(assemblerLine->modified, autoLine);
	assemblerInsertLine(program, assemblerLine, autoLineNext++);

	// Unless nostack set, add line to set the stack pointer (this is just reserving it for now)
	uint16_t stackSetLineIndex=0;
	if (!program->noStack) {
		sprintf(autoLine, "mov r%u 65535 ; setup stack", BytecodeRegisterSP);

		assemblerLine=malloc(sizeof(AssemblerLine));
		assemblerLine->lineNum=autoLineNext+1;
		assemblerLine->file=malloc(strlen(autoFile)+1);
		strcpy(assemblerLine->file, autoFile);
		assemblerLine->original=malloc(strlen(autoLine)+1);
		strcpy(assemblerLine->original, autoLine);
		assemblerLine->modified=malloc(strlen(autoLine)+1);
		strcpy(assemblerLine->modified, autoLine);
		assemblerInsertLine(program, assemblerLine, autoLineNext);
		stackSetLineIndex=autoLineNext++;
	}

	// Verbose output
	if (verbose) {
		printf("Non-blank input lines:\n");
		for(unsigned i=0; i<program->linesNext; ++i) {
			AssemblerLine *assemblerLine=program->lines[i];
			if (strlen(assemblerLine->modified)>0)
				printf("	%s:%4u '%s' -> '%s'\n", assemblerLine->file, assemblerLine->lineNum, assemblerLine->original, assemblerLine->modified);
		}
	}

	// Parse lines into initial instructions
	if (!assemblerProgramParseLines(program))
		goto done;

	// Move defines to be after everything else
	while(assemblerProgramShiftDefines(program))
		;

	// Shrink defines if possible
	while(assemblerProgramShrinkDefines(program))
		;

	// Generate initial guess at machine code offsets for each instruction
	if (!assemblerProgramCalculateInitialMachineCodeLengths(program))
		goto done;

	// Machine code generation loop
	do {
		// (re)-compute offsets for each instruction based on sum of lengths of all previous ones
		assemblerProgramCalculateMachineCodeOffsets(program);

		// compute machine code, potentially noticing size savings causing us to have to loop
		if (!assemblerProgramGenerateMachineCode(program, &change))
			goto done;

		// If any changes, loop again as we may now be able to make further changes.
	} while(change);

	// Update instruction we created earlier (if we did) to set the stack pointer register (now that we have computed offsets),
	// and then recompute machine code one last time.
	if (!program->noStack) {
		// TODO: Check for not finding
		for(unsigned i=0; i<program->instructionsNext; ++i) {
			AssemblerInstruction *instruction=&program->instructions[i];
			if (instruction->lineIndex==stackSetLineIndex) {
				// Update line to put correct ram offset in. No need to update dest or src pointers as the start of the string has not changed, and we know this is safe because the new offset cannot be longer than the one we used when allocating the line in the first place.
				sprintf(instruction->d.mov.src, "%u", program->stackRamOffset);
				break;
			}
		}
	}

	if (!assemblerProgramGenerateMachineCode(program, &change))
		goto done;
	assert(!change); // SP is always >=32kb so needs 3 byte set16 instruction regardless

	// Verbose output
	if (verbose)
		assemblerProgramDebugInstructions(program);

	// Output machine code
	if (!assemblerProgramWriteMachineCode(program, outputPath)) {
		printf("Could not write machine code to '%s'\n", outputPath);
		goto done;
	}

	// Tidy up
	done:
	assemblerProgramFree(program);

	return 0;
}

AssemblerProgram *assemblerProgramNew(void) {
	AssemblerProgram *program=malloc(sizeof(AssemblerProgram));
	if (program==NULL) {
		printf("Could not allocate memory for program data\n");
		return NULL;
	}

	program->linesNext=0;
	program->instructionsNext=0;
	program->includePathsNext=0;
	program->noStack=false;
	program->noScratch=false;
	program->assemblerIncludeDirsNext=0;

	return program;
}

void assemblerProgramFree(AssemblerProgram *program) {
	// NULL check
	if (program==NULL)
		return;

	// Free lines
	for(unsigned i=0; i<program->linesNext; ++i) {
		AssemblerLine *assemblerLine=program->lines[i];
		free(assemblerLine->file);
		free(assemblerLine->original);
		free(assemblerLine->modified);
		free(assemblerLine);
	}

	// Free instructions
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		free(instruction->modifiedLineCopy);
	}

	// Free struct memory
	free(program);
}

void assemblerInsertLine(AssemblerProgram *program, AssemblerLine *line, int offset) {
	assert(program!=NULL);
	assert(line!=NULL);
	assert(offset<=program->linesNext);

	memmove(program->lines+offset+1, program->lines+offset, sizeof(AssemblerLine *)*(program->linesNext-offset));
	program->lines[offset]=line;
	program->linesNext++;
}

bool assemblerInsertLinesFromFile(AssemblerProgram *program, const char *path, int offset) {
	assert(program!=NULL);
	assert(path!=NULL);

	// Open input file
	FILE *file=fopen(path, "r");
	if (file==NULL) {
		printf("Could not open input file '%s' for reading\n", path);
		return false;
	}

	// Read file line-by-line
	char *line=NULL;
	size_t lineSize=0;
	unsigned lineNum=1;
	while(getline(&line, &lineSize, file)>0) {
		// Trim trailing newline
		if (line[strlen(line)-1]=='\n')
			line[strlen(line)-1]='\0';

		// Begin creating structure to represent this line
		AssemblerLine *assemblerLine=malloc(sizeof(AssemblerLine));

		assemblerLine->lineNum=lineNum;
		assemblerLine->file=malloc(strlen(path)+1);
		strcpy(assemblerLine->file, path);
		assemblerLine->original=malloc(strlen(line)+1);
		strcpy(assemblerLine->original, line);
		assemblerLine->modified=malloc(strlen(line)+1);
		strcpy(assemblerLine->modified, line);

		assemblerInsertLine(program, assemblerLine, offset++);

		// Advance to next line
		++lineNum;
	}
	free(line);

	fclose(file);

	// Add to include paths array
	strcpy(program->includedPaths[program->includePathsNext++], path);

	return true;
}

void assemblerRemoveLine(AssemblerProgram *program, int offset) {
	assert(program!=NULL);
	assert(offset<program->linesNext);

	AssemblerLine *line=program->lines[offset];
	free(line->file);
	free(line->original);
	free(line->modified);
	free(line);

	memmove(program->lines+offset, program->lines+offset+1, sizeof(AssemblerLine *)*((--program->linesNext)-offset));
}

bool assemblerProgramPreprocess(AssemblerProgram *program) {
	assert(program!=NULL);

	bool change=false;

	// Strip comments and excess white space
	for(unsigned i=0; i<program->linesNext; ++i) {
		bool inString;
		char *c;
		AssemblerLine *assemblerLine=program->lines[i];

		// Convert all white-space to actual spaces, and strip of comment if any.
		inString=false;
		for(c=assemblerLine->modified; *c!='\0'; ++c) {
			if (inString) {
				if (*c=='\'')
					inString=false;
				else if (*c=='\\')
					++c; // Skip escaped character
			} else {
				if (*c=='\'') {
					inString=true;
				} else if (*c==';') {
					change=true;
					*c='\0';
					break;
				} else if (isspace(*c))
					*c=' ';
			}
		}

		// Replace two or more spaces with a single space (outside of strings)
		bool localChange;
		do {
			localChange=false;
			inString=false;
			for(c=assemblerLine->modified; *c!='\0'; ++c) {
				if (inString) {
					if (*c=='\'')
						inString=false;
					else if (*c=='\\')
						++c; // Skip escaped character
				} else {
					if (*c=='\'') {
						inString=true;
					} else if (*c==' ') {
						if (c[1]=='\0')
							break;
						else if (c[1]==' ') {
							memmove(c, c+1, strlen(c+1)+1);
							localChange=true;
							change=true;
						}
					}
				}
			}
		} while(localChange);

		// Trim preceeding or trailing white space.
		if (assemblerLine->modified[0]==' ') {
			memmove(assemblerLine->modified, assemblerLine->modified+1, strlen(assemblerLine->modified+1)+1);
			change=true;
		}
		if (strlen(assemblerLine->modified)>0 && assemblerLine->modified[strlen(assemblerLine->modified)-1]==' ') {
			assemblerLine->modified[strlen(assemblerLine->modified)-1]='\0';
			change=true;
		}
	}

	return change;
}

bool assemblerProgramLocateInclude(const AssemblerProgram *program, char *destPath, const char *callerPath, const char *srcPath) {
	assert(program!=NULL);
	assert(callerPath!=NULL);

	char tempPath[1024]={0}; // TODO: better

	// Try relative to directory include/require statement was in
	strcpy(destPath, callerPath);
	char *lastSlash=strrchr(destPath, '/');
	if (lastSlash!=NULL)
		lastSlash[1]='\0';
	else
		destPath[0]='\0';
	strcat(destPath, srcPath);
	pathNormalise(destPath);
	if (pathExists(destPath))
		goto done;

	// Try include dirs
	for(size_t i=0; i<program->assemblerIncludeDirsNext; ++i) {
		// Try relative to this dir
		const char *includeDirPath=program->assemblerIncludeDirs[i];
		strcpy(destPath, includeDirPath);
		strcat(destPath, "/");
		strcat(destPath, srcPath);
		pathNormalise(destPath);
		if (pathExists(destPath))
			goto done;
	}

	return false;

	done:
	// Normalise path
	if (destPath[0]!='/') {
		getcwd(tempPath, 1024);
		strcat(tempPath, "/");
	}
	strcat(tempPath, destPath);
	pathNormalise(tempPath);
	strcpy(destPath, tempPath);

	return true;
}

bool assemblerProgramHandleNextInclude(AssemblerProgram *program, bool *change) {
	assert(program!=NULL);

	if (change!=NULL)
		*change=false;

	// Loop over lines looking for those which start with 'include ' or 'require .
	for(unsigned line=0; line<program->linesNext; ++line) {
		AssemblerLine *assemblerLine=program->lines[line];

		// Check for include or require statement
		bool isInclude=(strncmp(assemblerLine->modified, "include ", strlen("include "))==0);
		bool isRequire=(strncmp(assemblerLine->modified, "require ", strlen("require "))==0);
		bool isRequireEnd=(strncmp(assemblerLine->modified, "requireend ", strlen("requireend "))==0);
		if (!isInclude && !isRequire && !isRequireEnd)
			continue;

		// Extract path
		char newPath[1024]; // TODO: Avoid hardcoded size
		bool located=assemblerProgramLocateInclude(program, newPath, assemblerLine->file, strchr(assemblerLine->modified, ' ')+1);
		if (!located)
			printf("warning - could not find file to include (%s:%u '%s')\n", assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);

		// Remove this line
		assemblerRemoveLine(program, line);

		// Indicate a change has occured
		if (change!=NULL)
			*change=true;

		if (!located)
			continue;

		// If using require rather than include, and this file has already been included/required, then skip
		if (isRequire || isRequireEnd) {
			bool alreadyIncluded=false;
			for(size_t i=0; i<program->includePathsNext; ++i)
				if (strcmp(program->includedPaths[i], newPath)==0) {
					alreadyIncluded=true;
					break;
				}
			if (alreadyIncluded) {
				--line;
				continue;
			}
		}

		// Read line-by-line
		int offset=(isRequireEnd ? program->linesNext : line);
		if (!assemblerInsertLinesFromFile(program, newPath, offset))
			return false;

		// We have handled something - return
		break;
	}

	return true;
}

bool assemblerProgramHandleNextOption(AssemblerProgram *program, bool *change) {
	assert(program!=NULL);

	if (change!=NULL)
		*change=false;

	// Loop over lines looking for those which start with 'nostack ' or 'noscratch .
	for(unsigned line=0; line<program->linesNext; ++line) {
		AssemblerLine *assemblerLine=program->lines[line];

		// Check for nostack or noscratch statement
		bool isNoStack=(strncmp(assemblerLine->modified, "nostack", strlen("nostack"))==0);
		bool isNoScratch=(strncmp(assemblerLine->modified, "noscratch", strlen("noscratch"))==0);
		if (!isNoStack && !isNoScratch)
			continue;

		// Remove this line
		assemblerRemoveLine(program, line);

		// Indicate a change has occured
		if (change!=NULL)
			*change=true;

		// Set relavent flag
		program->noStack|=isNoStack;
		program->noScratch|=isNoScratch;

		// We have handled something - return
		break;
	}

	return true;
}

bool assemblerProgramParseLines(AssemblerProgram *program) {
	assert(program!=NULL);

	// Parse lines
	for(unsigned i=0; i<program->linesNext; ++i) {
		AssemblerLine *assemblerLine=program->lines[i];

		// Skip empty lines
		if (strlen(assemblerLine->modified)==0)
			continue;

		// Parse operation
		char *lineCopy=malloc(strlen(assemblerLine->modified)+1);
		strcpy(lineCopy, assemblerLine->modified);

		char *savePtr;
		char *first=strtok_r(lineCopy, " ", &savePtr);
		if (first==NULL) {
			free(lineCopy);
			continue;
		}

		if (strcmp(first, "ab")==0 || strcmp(first, "aw")==0) {
			unsigned membSize=0;
			switch(first[1]) {
				case 'b': membSize=1; break;
				case 'w': membSize=2; break;
			}
			assert(membSize!=0);

			char *symbol=strtok_r(NULL, " ", &savePtr);
			if (symbol==NULL) {
				printf("error - expected symbol name after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			char *lenStr=strtok_r(NULL, " ", &savePtr);
			if (lenStr==NULL) {
				printf("error - expected length constant after '%s' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerRegisterFromStr(symbol)!=BytecodeRegisterNB) {
				printf("error - cannot use reserved symbol '%s' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetAllocationSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as an 'allocation' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetDefineSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a 'define' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetLabelSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a label (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetConstSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a constant (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			int allocationLen=assemblerGetConstSymbolValue(program, lenStr);
			if (allocationLen==-1)
				allocationLen=atoi(lenStr);

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeAllocation;
			instruction->d.allocation.membSize=membSize;
			instruction->d.allocation.len=allocationLen;
			instruction->d.allocation.totalSize=instruction->d.allocation.membSize*instruction->d.allocation.len;
			instruction->d.allocation.symbol=symbol;

			if (instruction->d.allocation.len==0)
				printf("warning - 0 length allocation (%s:%u '%s')\n", assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
		} else if (strcmp(first, "db")==0 || strcmp(first, "dw")==0) {
			unsigned membSize=0;
			switch(first[1]) {
				case 'b': membSize=1; break;
				case 'w': membSize=2; break;
			}
			assert(membSize!=0);

			char *symbol=strtok_r(NULL, " ", &savePtr);
			if (symbol==NULL) {
				printf("error - expected symbol name after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerRegisterFromStr(symbol)!=BytecodeRegisterNB) {
				printf("error - cannot use reserved symbol '%s' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetAllocationSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as an 'allocation' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetDefineSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a 'define' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetLabelSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a label (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetConstSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a constant (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeDefine;
			instruction->d.define.membSize=membSize;
			instruction->d.define.len=0;

			char tempInteger[32], *tempIntegerNext;
			tempIntegerNext=tempInteger;
			char *dataChar=symbol+strlen(symbol)+1; // TODO: This is not safe if no other data given
			bool inDataString=false;
			while(*dataChar!='\0') {
				if (inDataString) {
					if (*dataChar=='\'') {
						inDataString=false;
					} else if (*dataChar=='\\') {
						// Append special character constant by reading and skipping next byte
						if (*(dataChar+1)=='\0')
							break;
						++dataChar;
						switch(*dataChar) {
							case 'n':
								instruction->d.define.data[instruction->d.define.len++]='\n';
							break;
							case 't':
								instruction->d.define.data[instruction->d.define.len++]='\t';
							break;
						}
					} else {
						// Append character
						instruction->d.define.data[instruction->d.define.len++]=*dataChar;
					}
				} else {
					if (*dataChar=='\'') {
						inDataString=true;
					} else if (*dataChar==',') {
						if (tempIntegerNext>tempInteger) {
							// Append integer
							*tempIntegerNext++='\0';
							int16_t value=atoi(tempInteger);
							switch(instruction->d.define.membSize) {
								case 1:
									instruction->d.define.data[instruction->d.define.len++]=(int8_t)value;
								break;
								case 2:
									instruction->d.define.data[instruction->d.define.len*instruction->d.define.membSize]=(value>>8);
									instruction->d.define.data[instruction->d.define.len*instruction->d.define.membSize+1]=(value&0xFF);
									instruction->d.define.len++;
								break;
								default:
									assert(false); // TODO: Handle better
								break;
							}
							tempIntegerNext=tempInteger;
						}
					} else if (isdigit(*dataChar) || *dataChar=='-') { // FIXME: allows minus sign anywhere in constant
						// Add digit to integer
						// TODO: Handle this better - we currently simply ignore non-numeric characters
						*tempIntegerNext++=*dataChar;
					}
				}

				++dataChar;
			}

			if (tempIntegerNext>tempInteger) {
				// Append integer
				*tempIntegerNext++='\0';
				int16_t value=atoi(tempInteger);
				switch(instruction->d.define.membSize) {
					case 1:
						instruction->d.define.data[instruction->d.define.len++]=(int8_t)value;
					break;
					case 2:
						instruction->d.define.data[instruction->d.define.len*instruction->d.define.membSize]=(value>>8);
						instruction->d.define.data[instruction->d.define.len*instruction->d.define.membSize+1]=(value&0xFF);
						instruction->d.define.len++;
					break;
					default:
						assert(false); // TODO: Handle better
					break;
				}
				tempIntegerNext=tempInteger;
			}

			instruction->d.define.totalSize=instruction->d.define.membSize*instruction->d.define.len;
			instruction->d.define.pointerLineIndex=instruction->lineIndex;
			instruction->d.define.pointerOffset=0;
			instruction->d.define.symbol=symbol;
		} else if (strcmp(first, "mov")==0) {
			char *dest=strtok_r(NULL, " ", &savePtr);
			if (dest==NULL) {
				printf("error - expected dest after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			// Note: we cannot use strtok here as it might insert extra earlier NULL in case of e.g. character constant ' '
			char *src=dest+strlen(dest)+1;
			if (*src=='\0') {
				printf("error - expected src after '%s' (%s:%u '%s')\n", dest, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeMov;
			instruction->d.mov.dest=dest;
			instruction->d.mov.src=src;
		} else if (strcmp(first, "label")==0) {
			char *symbol=strtok_r(NULL, " ", &savePtr);
			if (symbol==NULL) {
				printf("error - expected symbol after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerRegisterFromStr(symbol)!=BytecodeRegisterNB) {
				printf("error - cannot use reserved symbol '%s' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetAllocationSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as an 'allocation' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetDefineSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a 'define' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetLabelSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a label (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetConstSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a constant (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeLabel;
			instruction->d.label.symbol=symbol;
		} else if (strcmp(first, "syscall")==0) {
			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeSyscall;
		} else if (strcmp(first, "clricache")==0) {
			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeClearInstructionCache;
		} else if (strcmp(first, "debug")==0) {
			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeDebug;
		} else if (strcmp(first, "jmp")==0) {
			char *addr=strtok_r(NULL, " ", &savePtr);
			if (addr==NULL) {
				printf("error - expected address label after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeJmp;
			instruction->d.jmp.addr=addr;
		} else if (strcmp(first, "push8")==0) {
			char *src=strtok_r(NULL, " ", &savePtr);
			if (src==NULL) {
				printf("error - expected src register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypePush8;
			instruction->d.push8.src=src;
		} else if (strcmp(first, "pop8")==0) {
			char *dest=strtok_r(NULL, " ", &savePtr);
			if (dest==NULL) {
				printf("error - expected dest register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypePop8;
			instruction->d.pop8.dest=dest;
		} else if (strcmp(first, "call")==0) {
			char *label=strtok_r(NULL, " ", &savePtr);
			if (label==NULL) {
				printf("error - expected label register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeCall;
			instruction->d.call.label=label;
		} else if (strcmp(first, "ret")==0) {
			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeRet;
		} else if (strcmp(first, "store8")==0) {
			char *dest=strtok_r(NULL, " ", &savePtr);
			if (dest==NULL) {
				printf("error - expected dest addr register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			char *src=strtok_r(NULL, " ", &savePtr);
			if (src==NULL) {
				printf("error - expected src register after '%s' (%s:%u '%s')\n", dest, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeStore8;
			instruction->d.store8.dest=dest;
			instruction->d.store8.src=src;
		} else if (strcmp(first, "load8")==0) {
			char *dest=strtok_r(NULL, " ", &savePtr);
			if (dest==NULL) {
				printf("error - expected dest register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			char *src=strtok_r(NULL, " ", &savePtr);
			if (src==NULL) {
				printf("error - expected src addr register after '%s' (%s:%u '%s')\n", dest, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeLoad8;
			instruction->d.load8.dest=dest;
			instruction->d.load8.src=src;
		} else if (strcmp(first, "xchg8")==0) {
			char *addrReg=strtok_r(NULL, " ", &savePtr);
			if (addrReg==NULL) {
				printf("error - expected addr register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			char *srcDestReg=strtok_r(NULL, " ", &savePtr);
			if (srcDestReg==NULL) {
				printf("error - expected src/dest register after '%s' (%s:%u '%s')\n", addrReg, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeXchg8;
			instruction->d.xchg8.addrReg=addrReg;
			instruction->d.xchg8.srcDestReg=srcDestReg;
		} else if (strcmp(first, "const")==0) {
			char *symbol=strtok_r(NULL, " ", &savePtr);
			if (symbol==NULL) {
				printf("error - expected symbol name after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerRegisterFromStr(symbol)!=BytecodeRegisterNB) {
				printf("error - cannot use reserved symbol '%s' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetAllocationSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as an 'allocation' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetDefineSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a 'define' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetLabelSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a label (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			if (assemblerGetConstSymbolInstructionIndex(program, symbol)!=-1) {
				printf("error - symbol '%s' already defined as a constant (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			char *valueStr=strtok_r(NULL, " ", &savePtr);
			if (valueStr==NULL) {
				printf("error - expected value name after '%s' (%s:%u '%s')\n", symbol, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			// TODO: Support single character constants
			// TODO: Better error checking for bad constant (currently just defaults to 0)

			int constValue;
			if ((constValue=assemblerGetConstSymbolValue(program, valueStr))==-1)
				constValue=atoi(valueStr);

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeConst;
			instruction->d.constSymbol.symbol=symbol;
			instruction->d.constSymbol.value=constValue;
		} else if (strcmp(first, "nop")==0) {
			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeNop;
		} else {
			// Check for an ALU operation
			unsigned j;
			for(j=0; j<sizeof(assemblerInstructionAluData)/sizeof(assemblerInstructionAluData[0]); ++j)
				if (strcmp(first, assemblerInstructionAluData[j].str)==0)
					break;

			if (j!=sizeof(assemblerInstructionAluData)/sizeof(assemblerInstructionAluData[0])) {
				// ALU op
				char *dest=strtok_r(NULL, " ", &savePtr);
				if (dest==NULL) {
					printf("error - expected dest after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
					return false;
				}

				char *opA=NULL, *opB=NULL;

				if (assemblerInstructionAluData[j].ops>=1) {
					opA=strtok_r(NULL, " ", &savePtr);
					if (opA==NULL) {
						printf("error - expected operand A after '%s' (%s:%u '%s')\n", dest, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
						return false;
					}
				}

				if (assemblerInstructionAluData[j].ops>=2) {
					opB=strtok_r(NULL, " ", &savePtr);
					if (opB==NULL) {
						printf("error - expected operand B after '%s' (%s:%u '%s')\n", opA, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
						return false;
					}
				}

				AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
				instruction->lineIndex=i;
				instruction->modifiedLineCopy=lineCopy;
				instruction->type=AssemblerInstructionTypeAlu;
				instruction->d.alu.type=assemblerInstructionAluData[j].type;
				if (instruction->d.alu.type==BytecodeInstructionAluTypeSkip)
					instruction->d.alu.skipBit=assemblerInstructionAluData[j].skipBit;
				if (instruction->d.alu.type==BytecodeInstructionAluTypeInc || instruction->d.alu.type==BytecodeInstructionAluTypeDec)
					instruction->d.alu.incDecValue=assemblerInstructionAluData[j].incDecValue;
				if (instruction->d.alu.type==BytecodeInstructionAluTypeExtra)
					instruction->d.alu.extraType=assemblerInstructionAluData[j].extraType;
				instruction->d.alu.dest=dest;
				instruction->d.alu.opA=opA;
				instruction->d.alu.opB=opB;
			} else {
				printf("error - unknown/unimplemented instruction '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				free(lineCopy);
				return false;
			}
		}
	}

	return true;
}

bool assemblerProgramShiftDefines(AssemblerProgram *program) {
	assert(program!=NULL);

	// Move defines to be after everything else
	bool anyChange=false;
	bool change;
	do {
		change=false;
		for(unsigned i=0; i<program->instructionsNext-1; ++i) {
				AssemblerInstruction *instruction=&program->instructions[i];
				AssemblerInstruction *nextInstruction=&program->instructions[i+1];

				if (instruction->type==AssemblerInstructionTypeDefine && nextInstruction->type!=AssemblerInstructionTypeDefine) {
					// Swap
					AssemblerInstruction tempInstruction=*instruction;
					*instruction=*nextInstruction;
					*nextInstruction=tempInstruction;
					change=true;
				}
		}
		anyChange|=change;
	} while(change);
	return anyChange;
}

bool assemblerProgramShrinkDefines(AssemblerProgram *program) {
	assert(program!=NULL);

	// Look for defines whoose data is completely found within another
	bool anyChange=false;
	bool change;
	do {
		change=false;
		for(unsigned i=0; i<program->instructionsNext-1; ++i) {
			AssemblerInstruction *instruction=&program->instructions[i];
			if (instruction->type!=AssemblerInstructionTypeDefine)
				continue;
			if (instruction->d.define.pointerLineIndex!=instruction->lineIndex)
				continue; // already handled

			for(unsigned j=0; j<program->instructionsNext-1; ++j) {
				if (i==j)
					continue;
				AssemblerInstruction *loopInstruction=&program->instructions[j];
				if (loopInstruction->type!=AssemblerInstructionTypeDefine)
					continue;
				if (loopInstruction->d.define.pointerLineIndex!=loopInstruction->lineIndex)
					continue; // no point using this instruction as if we are a subset of it, we are also a subset of whatever this points to currently

				// We have found a pair of defines
				// Check if the main define's data exists within the loop define's data
				for(uint16_t loopDataI=0; loopDataI<=loopInstruction->d.define.totalSize-instruction->d.define.totalSize; ++loopDataI) {
					if (memcmp(instruction->d.define.data, loopInstruction->d.define.data+loopDataI, instruction->d.define.totalSize)==0) {
						// Match found - update this instruction to simply point into loop instruction
						instruction->d.define.pointerLineIndex=loopInstruction->lineIndex;
						instruction->d.define.pointerOffset=loopDataI;

						change=true;

						break;
					}
				}

				if (instruction->d.define.pointerLineIndex!=instruction->lineIndex)
					break; // found match
			}
		}
		anyChange|=change;
	} while(change);
	return anyChange;
}

bool assemblerProgramCalculateInitialMachineCodeLengths(AssemblerProgram *program) {
	assert(program!=NULL);

	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		AssemblerLine *line=program->lines[instruction->lineIndex];

		switch(instruction->type) {
			case AssemblerInstructionTypeAllocation:
				// These are reserved in RAM not program memory
				instruction->machineCodeLen=0;
				instruction->machineCodeInstructions=0;
			break;
			case AssemblerInstructionTypeDefine:
				// If we are not pointing into some other define's space, we will end up simply copying data into program memory directly
				instruction->machineCodeLen=0;
				if (instruction->d.define.pointerLineIndex==instruction->lineIndex)
					instruction->machineCodeLen+=instruction->d.define.totalSize;
				instruction->machineCodeInstructions=0;
			break;
			case AssemblerInstructionTypeMov: {
				// Check src and determine type
				BytecodeRegister srcReg;
				int defineAddr, allocationAddr, constValue, labelAddr;
				if (isdigit(instruction->d.mov.src[0])) {
					// Integer - use set4, set8 or set16 instruction as needed
					BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.mov.dest);
					unsigned value=atoi(instruction->d.mov.src);
					if (value<16 && destReg<4)
						instruction->machineCodeLen=1; // will use set4 instruction
					else if (value<256)
						instruction->machineCodeLen=2; // will use set8 instruction
					else
						instruction->machineCodeLen=3; // will use set16 instruction
				} else if ((srcReg=assemblerRegisterFromStr(instruction->d.mov.src))!=BytecodeRegisterNB) {
					instruction->machineCodeLen=2; // will use alu OR instruction
				} else if (instruction->d.mov.src[0]=='\'') {
					char c=instruction->d.mov.src[1];
					if (!(isprint(c) || c=='\t') || c=='\'' || instruction->d.mov.src[strlen(instruction->d.mov.src)-1]!='\'') {
						printf("error - bad character constant '%s' (%s:%u '%s')\n", instruction->d.mov.src, line->file, line->lineNum, line->original);
						return false;
					}

					if (c=='\\') {
						c=instruction->d.mov.src[2];
						switch(c) {
							case 'n': c='\n'; break;
							case 'r': c='\r'; break;
							case 't': c='\t'; break;
							default:
								printf("error - bad escaped character constant '%s' (expecting n, r or t) (%s:%u '%s')\n", instruction->d.mov.src, line->file, line->lineNum, line->original);
								return false;
							break;
						}
					}

					instruction->machineCodeLen=2; // will use set8 instruction
				} else if ((defineAddr=assemblerGetDefineSymbolAddr(program, instruction->d.mov.src))!=-1) {
					// Define symbol may need set16
					instruction->machineCodeLen=3;
				} else if ((allocationAddr=assemblerGetAllocationSymbolAddr(program, instruction->d.mov.src))!=-1) {
					// Allocation symbol may need set16
					instruction->machineCodeLen=3;
				} else if ((labelAddr=assemblerGetLabelSymbolAddr(program, instruction->d.mov.src))!=-1) {
					// Label symbol may need set16
					instruction->machineCodeLen=3;
				} else if ((constValue=assemblerGetConstSymbolValue(program, instruction->d.mov.src))!=-1) {
					BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.mov.dest);
					if (constValue<16 && destReg<4)
						instruction->machineCodeLen=1; // use set4
					else if (constValue<256)
						instruction->machineCodeLen=2; // use set8
					else
						instruction->machineCodeLen=3; // use set16
				} else {
					printf("error - bad src '%s' (%s:%u '%s')\n", instruction->d.mov.src, line->file, line->lineNum, line->original);
					return false;
				}

				instruction->machineCodeInstructions=1;
			} break;
			case AssemblerInstructionTypeLabel:
				instruction->machineCodeLen=0;
				instruction->machineCodeInstructions=0;
			break;
			case AssemblerInstructionTypeSyscall:
				instruction->machineCodeLen=1;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeClearInstructionCache:
				instruction->machineCodeLen=1;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeDebug:
				instruction->machineCodeLen=1;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeAlu:
				instruction->machineCodeLen=2; // all ALU instructions take 2 bytes
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeJmp:
				instruction->machineCodeLen=3; // Reserve three bytes for set16 instruction
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypePush8:
				instruction->machineCodeLen=3; // Reserve three bytes (store8 + inc1)
				instruction->machineCodeInstructions=2;
			break;
			case AssemblerInstructionTypePop8:
				instruction->machineCodeLen=3; // Reserve three bytes (dec1 + load8)
				instruction->machineCodeInstructions=2;
			break;
			case AssemblerInstructionTypeCall:
				instruction->machineCodeLen=5;
				instruction->machineCodeInstructions=2;
			break;
			case AssemblerInstructionTypeRet:
				instruction->machineCodeLen=2;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeStore8:
				instruction->machineCodeLen=1;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeLoad8:
				instruction->machineCodeLen=1;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeXchg8:
				instruction->machineCodeLen=2;
				instruction->machineCodeInstructions=1;
			break;
			case AssemblerInstructionTypeConst:
				instruction->machineCodeLen=0;
				instruction->machineCodeInstructions=0;
			break;
			case AssemblerInstructionTypeNop:
				instruction->machineCodeLen=1;
				instruction->machineCodeInstructions=1;
			break;
		}
	}

	return true;
}

void assemblerProgramCalculateMachineCodeOffsets(AssemblerProgram *program) {
	assert(program!=NULL);

	unsigned nextMachineCodeOffset=BytecodeMemoryProgmemAddr;
	unsigned nextRamOffset=BytecodeMemoryRamAddr;
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];

		instruction->machineCodeOffset=nextMachineCodeOffset;
		nextMachineCodeOffset+=instruction->machineCodeLen;

		if (instruction->type==AssemblerInstructionTypeAllocation) {
			instruction->d.allocation.ramOffset=nextRamOffset;
			nextRamOffset+=instruction->d.allocation.totalSize;
		}
	}

	program->stackRamOffset=nextRamOffset;
}

bool assemblerProgramGenerateMachineCode(AssemblerProgram *program, bool *changeFlag) {
	assert(program!=NULL);

	if (changeFlag!=NULL)
		*changeFlag=false;

	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		AssemblerLine *line=program->lines[instruction->lineIndex];

		// Clear machine code array to invalid bytes
		memset(instruction->machineCode, ByteCodeIllegalInstructionByte, AssemblerInstructionMachineCodeMax);

		// Type-specific generation
		switch(instruction->type) {
			case AssemblerInstructionTypeAllocation:
			break;
			case AssemblerInstructionTypeDefine:
				// If we are not pointing into some other define's space, simply copy data into program memory directly
				if (instruction->d.define.pointerLineIndex==instruction->lineIndex)
					memcpy(instruction->machineCode, instruction->d.define.data, instruction->d.define.totalSize);
			break;
			case AssemblerInstructionTypeMov: {
				// Verify dest is a valid register
				BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.mov.dest);
				if (destReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as destination, instead got '%s' (%s:%u '%s')\n", instruction->d.mov.dest, line->file, line->lineNum, line->original);
					return false;
				}

				// Determine type of src (we have already verified it in previous pass)
				BytecodeRegister srcReg;
				int defineAddr, allocationAddr, constValue, labelAddr;
				if (isdigit(instruction->d.mov.src[0])) {
					// Integer - use set4, set8 or set16 instruction as needed
					unsigned value=atoi(instruction->d.mov.src);
					bytecodeInstructionCreateSet(instruction->machineCode, destReg, value);
				} else if ((srcReg=assemblerRegisterFromStr(instruction->d.mov.src))!=BytecodeRegisterNB) {
					// Register - use dest=src|src as a copy
					BytecodeInstruction2Byte copyOp=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeOr, destReg, srcReg, srcReg);
					instruction->machineCode[0]=(copyOp>>8);
					instruction->machineCode[1]=(copyOp&0xFF);
				} else if (instruction->d.mov.src[0]=='\'') {
					char c=instruction->d.mov.src[1];
					if (c=='\\') {
						c=instruction->d.mov.src[2];
						switch(c) {
							case 'n': c='\n'; break;
							case 'r': c='\r'; break;
							case 't': c='\t'; break;
						}
					}
					bytecodeInstructionCreateSet(instruction->machineCode, destReg, c);
				} else if ((defineAddr=assemblerGetDefineSymbolAddr(program, instruction->d.mov.src))!=-1)
					bytecodeInstructionCreateSet(instruction->machineCode, destReg, defineAddr);
				else if ((allocationAddr=assemblerGetAllocationSymbolAddr(program, instruction->d.mov.src))!=-1)
					bytecodeInstructionCreateSet(instruction->machineCode, destReg, allocationAddr);
				else if ((labelAddr=assemblerGetLabelSymbolAddr(program, instruction->d.mov.src))!=-1)
					bytecodeInstructionCreateSet(instruction->machineCode, destReg, labelAddr);
				else if ((constValue=assemblerGetConstSymbolValue(program, instruction->d.mov.src))!=-1)
					bytecodeInstructionCreateSet(instruction->machineCode, destReg, constValue);
				else {
					printf("error - bad src '%s' (%s:%u '%s')\n", instruction->d.mov.src, line->file, line->lineNum, line->original);
					return false;
				}
			} break;
			case AssemblerInstructionTypeLabel:
			break;
			case AssemblerInstructionTypeSyscall:
				instruction->machineCode[0]=bytecodeInstructionCreateMiscSyscall();
			break;
			case AssemblerInstructionTypeClearInstructionCache:
				instruction->machineCode[0]=bytecodeInstructionCreateMiscClearInstructionCache();
			break;
			case AssemblerInstructionTypeDebug:
				instruction->machineCode[0]=bytecodeInstructionCreateMiscDebug();
			break;
			case AssemblerInstructionTypeAlu: {
				// Special case for push16 and pop16 as these require the stack register - fail if we cannot use it
				if (program->noStack && instruction->d.alu.type==BytecodeInstructionAluTypeExtra) {
					if (instruction->d.alu.extraType==BytecodeInstructionAluExtraTypePush16) {
						printf("error - push16 requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
						return false;
					}
					if (instruction->d.alu.extraType==BytecodeInstructionAluExtraTypePop16) {
						printf("error - pop16 requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
						return false;
					}
				}

				// Verify dest is a valid register
				BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.alu.dest);
				if (destReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as destination, instead got '%s' (%s:%u '%s')\n", instruction->d.alu.dest, line->file, line->lineNum, line->original);
					return false;
				}

				// Read operand registers
				// TODO: This better (i.e. check how many we expect and error if not enough)
				BytecodeRegister opAReg=assemblerRegisterFromStr(instruction->d.alu.opA);
				BytecodeRegister opBReg=assemblerRegisterFromStr(instruction->d.alu.opB);
				if (opAReg==BytecodeRegisterNB)
					opAReg=0;
				if (opBReg==BytecodeRegisterNB)
					opBReg=0;

				// Create instruction
				if (instruction->d.alu.type==BytecodeInstructionAluTypeSkip) {
					// Special case to encode literal bit index and skip distance
					AssemblerInstruction *nextInstruction=&program->instructions[i+1];
					opAReg=instruction->d.alu.skipBit;
					opBReg=(nextInstruction!=NULL ? (nextInstruction->machineCodeInstructions)-1 : 0); // skip next pseudo instruction, which may be several raw instructions
				} else if (instruction->d.alu.type==BytecodeInstructionAluTypeInc || instruction->d.alu.type==BytecodeInstructionAluTypeDec) {
					// Special case to encode literal add/sub delta
					opAReg=(instruction->d.alu.incDecValue-1)>>3;
					opBReg=(instruction->d.alu.incDecValue-1)&7;
				} else if (instruction->d.alu.type==BytecodeInstructionAluTypeExtra) {
					// Special case to store type
					opBReg=instruction->d.alu.extraType;

					// Special case for push16 and pop16 to indicate stack register
					if (opBReg==BytecodeInstructionAluExtraTypePush16) {
						opAReg=destReg;
						destReg=BytecodeRegisterSP;
					} else if (opBReg==BytecodeInstructionAluExtraTypePop16)
						opAReg=BytecodeRegisterSP;
				}

				BytecodeInstruction2Byte aluOp=bytecodeInstructionCreateAlu(instruction->d.alu.type, destReg, opAReg, opBReg);

				instruction->machineCode[0]=(aluOp>>8);
				instruction->machineCode[1]=(aluOp&0xFF);
			} break;
			case AssemblerInstructionTypeJmp: {
				// Search through instructions looking for the label being defined
				int addr=assemblerGetLabelSymbolAddr(program, instruction->d.jmp.addr);
				if (addr==-1) {
					printf("error - bad jump label '%s' (%s:%u '%s')\n", instruction->d.jmp.addr, line->file, line->lineNum, line->original);
					return false;
				}

				// Create instruction which sets or adjusts the IP register

				// Check for trivial case of jumping to a label defined immediately after jump instruction.
				if (addr==instruction->machineCodeOffset+instruction->machineCodeLen)
					break; // 'nop' in the most literal sense - no code needed

				// Inspect previous iteration length and try to improve
				if (instruction->machineCodeLen==3) {
					BytecodeWord len2Addr=(addr>instruction->machineCodeOffset ? addr-1 : addr); // value addr will have next iteration if we do manage to reduce down to 2 bytes

					// set8 at 2 bytes?
					if (len2Addr<256) {
						// No need to generate proper instruction here - will be handled next iteration in machineCodeLen==2 case.
						instruction->machineCode[0]=bytecodeInstructionCreateMiscNop();
						instruction->machineCode[1]=bytecodeInstructionCreateMiscNop();
						break;
					}

					// inc/dec?
					BytecodeWord ip=instruction->machineCodeOffset+2; // value IP register will have at the time this instruction will be executed (assuming we manage 2 byte instruction)
					if (len2Addr>ip) {
						// inc case
						BytecodeWord jumpDistance=len2Addr-ip;
						if (jumpDistance<=64) {
							// No need to generate proper instruction here - will be handled next iteration in machineCodeLen==2 case.
							instruction->machineCode[0]=bytecodeInstructionCreateMiscNop();
							instruction->machineCode[1]=bytecodeInstructionCreateMiscNop();
							break;
						}
					} else if (len2Addr<ip) {
						// dec case
						BytecodeWord jumpDistance=ip-len2Addr;
						if (jumpDistance<=64) {
							// No need to generate proper instruction here - will be handled next iteration in machineCodeLen==2 case.
							instruction->machineCode[0]=bytecodeInstructionCreateMiscNop();
							instruction->machineCode[1]=bytecodeInstructionCreateMiscNop();
							break;
						}
					} else
						assert(false); // we have len2Addr=ip => addr>machineCodeOffset with len2Addr=addr-1 so addr=machineCodeOffset+machineCodeLen, which should have been handled by trivial case

					// set16 at 3 bytes as last resort
					bytecodeInstructionCreateMiscSet16(instruction->machineCode, BytecodeRegisterIP, addr);
					break;
				} else if (instruction->machineCodeLen==2) {
					// set8 at 2 bytes?
					if (addr<256) {
						BytecodeInstruction2Byte set8Op=bytecodeInstructionCreateMiscSet8(BytecodeRegisterIP, addr);
						instruction->machineCode[0]=(set8Op>>8);
						instruction->machineCode[1]=(set8Op&0xFF);
						break;
					}

					// Otherwise we must be able to do inc/dec (given we managed last time in 2 bytes)
					// Note: we know addr is correct for this instruction being 2 bytes long due to machineCodeLen check above.
					BytecodeWord ip=instruction->machineCodeOffset+instruction->machineCodeLen; // value IP register will have at the time this instruction will be executed
					if (addr>ip) {
						// inc case
						BytecodeWord jumpDistance=addr-ip;
						if (jumpDistance<=64) {
							BytecodeInstruction2Byte incOp=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeInc, BytecodeRegisterIP, jumpDistance);
							instruction->machineCode[0]=(incOp>>8);
							instruction->machineCode[1]=(incOp&0xFF);
							break;
						}
					} else if (addr<ip) {
						// dec case
						BytecodeWord jumpDistance=ip-addr;
						if (jumpDistance<=64) {
							BytecodeInstruction2Byte decOp=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeDec, BytecodeRegisterIP, jumpDistance);
							instruction->machineCode[0]=(decOp>>8);
							instruction->machineCode[1]=(decOp&0xFF);
							break;
						}
					} else
						assert(false); // we have addr=ip => addr=machineCodeOffset+machineCodeLen, but this should have been caught in initial trivial case

					// Internal error - last iteration we were promised a 2 byte instruction but cannot find it this time around
					assert(false);
				} else {
					// machineCodeLen==0 is valid also but is handled in trivial case above
					// otherwise only valid lengths are 3 for set16 and 2 for set8/inc/dec
					assert(false);
				}

				// We should have called 'break' in every case above
				assert(false);
			} break;
			case AssemblerInstructionTypePush8: {
				// This requires the stack register - fail if we cannot use it
				if (program->noStack) {
					printf("error - push8 requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Verify src is a valid register
				BytecodeRegister srcReg=assemblerRegisterFromStr(instruction->d.push8.src);
				if (srcReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as src, instead got '%s' (%s:%u '%s')\n", instruction->d.push8.src, line->file, line->lineNum, line->original);
					return false;
				}

				// Create as two instructions: store8 SP srcReg; inc1 SP
				instruction->machineCode[0]=bytecodeInstructionCreateMemory(BytecodeInstructionMemoryTypeStore8, BytecodeRegisterSP, srcReg);

				BytecodeInstruction2Byte inc1Op=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeInc, BytecodeRegisterSP, 1);
				instruction->machineCode[1]=(inc1Op>>8);
				instruction->machineCode[2]=(inc1Op&0xFF);
			} break;
			case AssemblerInstructionTypePop8: {
				// This requires the stack register - fail if we cannot use it
				if (program->noStack) {
					printf("error - pop8 requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Verify dest is a valid register
				BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.pop8.dest);
				if (destReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as dest, instead got '%s' (%s:%u '%s')\n", instruction->d.pop8.dest, line->file, line->lineNum, line->original);
					return false;
				}

				// Create as two instructions: dec1 SP; load16 destReg SP
				BytecodeInstruction2Byte dec1Op=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeDec, BytecodeRegisterSP, 1);
				instruction->machineCode[0]=(dec1Op>>8);
				instruction->machineCode[1]=(dec1Op&0xFF);

				instruction->machineCode[2]=bytecodeInstructionCreateMemory(BytecodeInstructionMemoryTypeLoad8, destReg, BytecodeRegisterSP);
			} break;
			case AssemblerInstructionTypeCall: {
				// This requires the scratch register - fail if we cannot use it
				if (program->noScratch) {
					printf("error - ret requires scratch register but noscratch set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// This requires the stack register - fail if we cannot use it
				if (program->noStack) {
					printf("error - call requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Search through instructions looking for the label being defined
				int addr=assemblerGetLabelSymbolAddr(program, instruction->d.call.label);
				if (addr==-1) {
					printf("error - bad call label '%s' (%s:%u '%s')\n", instruction->d.call.label, line->file, line->lineNum, line->original);
					return false;
				}

				// Create instructions (push adjusted IP onto stack and jump into function)
				// set8/16 rS addr
				unsigned setLength=bytecodeInstructionCreateSet(instruction->machineCode, BytecodeRegisterS, addr);

				// call rS rSP
				BytecodeInstruction2Byte callOp=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeExtra, BytecodeRegisterS, BytecodeRegisterSP, (BytecodeRegister)BytecodeInstructionAluExtraTypeCall);
				instruction->machineCode[setLength+0]=(callOp>>8);
				instruction->machineCode[setLength+1]=(callOp&0xFF);
			} break;
			case AssemblerInstructionTypeRet: {
				// This requires the stack register - fail if we cannot use it
				if (program->noStack) {
					printf("error - ret requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Create instructions (pop16 to pop ret addr off stack and jump back)
				BytecodeInstruction2Byte pop16Op=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeExtra, BytecodeRegisterIP, BytecodeRegisterSP, (BytecodeRegister)BytecodeInstructionAluExtraTypePop16);
				instruction->machineCode[0]=(pop16Op>>8);
				instruction->machineCode[1]=(pop16Op&0xFF);
			} break;
			case AssemblerInstructionTypeStore8: {
				// Verify dest and src are valid registers
				BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.store8.dest);
				if (destReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as destination address, instead got '%s' (%s:%u '%s')\n", instruction->d.store8.dest, line->file, line->lineNum, line->original);
					return false;
				}

				BytecodeRegister srcReg=assemblerRegisterFromStr(instruction->d.store8.src);
				if (srcReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as src, instead got '%s' (%s:%u '%s')\n", instruction->d.store8.src, line->file, line->lineNum, line->original);
					return false;
				}

				// Create instruction
				instruction->machineCode[0]=bytecodeInstructionCreateMemory(BytecodeInstructionMemoryTypeStore8, destReg, srcReg);
			} break;
			case AssemblerInstructionTypeLoad8: {
				// Verify dest and src are valid registers
				BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.load8.dest);
				if (destReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as destination, instead got '%s' (%s:%u '%s')\n", instruction->d.load8.dest, line->file, line->lineNum, line->original);
					return false;
				}

				BytecodeRegister srcReg=assemblerRegisterFromStr(instruction->d.load8.src);
				if (srcReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as src address, instead got '%s' (%s:%u '%s')\n", instruction->d.load8.src, line->file, line->lineNum, line->original);
					return false;
				}

				// Create instruction
				instruction->machineCode[0]=bytecodeInstructionCreateMemory(BytecodeInstructionMemoryTypeLoad8, destReg, srcReg);
			} break;
			case AssemblerInstructionTypeXchg8: {
				// Verify dest and src are valid registers
				BytecodeRegister addrReg=assemblerRegisterFromStr(instruction->d.xchg8.addrReg);
				if (addrReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as addr reg, instead got '%s' (%s:%u '%s')\n", instruction->d.xchg8.addrReg, line->file, line->lineNum, line->original);
					return false;
				}

				BytecodeRegister srcDestReg=assemblerRegisterFromStr(instruction->d.xchg8.srcDestReg);
				if (srcDestReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as src/dest reg, instead got '%s' (%s:%u '%s')\n", instruction->d.xchg8.srcDestReg, line->file, line->lineNum, line->original);
					return false;
				}

				// Create instruction
				BytecodeInstruction2Byte xchg8Op=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeExtra, addrReg, srcDestReg, (BytecodeRegister)BytecodeInstructionAluExtraTypeXchg8);
				instruction->machineCode[0]=(xchg8Op>>8);
				instruction->machineCode[1]=(xchg8Op&0xFF);
			} break;
			case AssemblerInstructionTypeConst:
			break;
			case AssemblerInstructionTypeNop:
				instruction->machineCode[0]=bytecodeInstructionCreateMiscNop();
			break;
		}

		// Check if we have ended up using less bytes than we thought for this instruction.
		if (instruction->machineCodeLen>0) {
			// Calculate actual number of btyes we ended up using for this instruction.
			unsigned actualLen=0;
			while(actualLen<AssemblerInstructionMachineCodeMax) {
				// Hit reserved value indicating end of instructions
				if (instruction->machineCode[actualLen]==ByteCodeIllegalInstructionByte)
					break;

				// Otherwise parse instruction's length and add to running total.
				actualLen+=bytecodeInstructionParseLength(instruction->machineCode+actualLen);
			}

			// Have we saved space since last iteration?
			if (actualLen!=instruction->machineCodeLen) {
				// We have - offsets will need adjusting and code regenerating.
				assert(actualLen<instruction->machineCodeLen);
				instruction->machineCodeLen=actualLen;
				if (changeFlag!=NULL) {
					*changeFlag=true;
					break;
				}
			}
		} else
			assert(instruction->machineCode[0]==ByteCodeIllegalInstructionByte);
	}

	return true;
}

void assemblerProgramDebugInstructions(const AssemblerProgram *program) {
	assert(program!=NULL);

	printf("Instructions:\n");
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *instruction=&program->instructions[i];
		const AssemblerLine *line=program->lines[instruction->lineIndex];

		printf("	%6u 0x%04X ", i, instruction->machineCodeOffset);
		for(unsigned j=0; j<instruction->machineCodeLen; ++j)
			printf("%02X", instruction->machineCode[j]);
		printf(": ");

		switch(instruction->type) {
			case AssemblerInstructionTypeAllocation:
				printf("allocation membSize=%u, len=%u, totalSize=%u, symbol=%s, ramOffset=%04X (%s:%u '%s')\n", instruction->d.allocation.membSize, instruction->d.allocation.len, instruction->d.allocation.totalSize, instruction->d.allocation.symbol, instruction->d.allocation.ramOffset, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeDefine:
				printf("define membSize=%u, len=%u, totalSize=%u, symbol=%s data=[", instruction->d.define.membSize, instruction->d.define.len, instruction->d.define.totalSize, instruction->d.define.symbol);
				for(unsigned j=0; j<instruction->d.define.len; j++) {
					unsigned value;
					switch(instruction->d.define.membSize) {
						case 1:
							value=instruction->d.define.data[j];
						break;
						case 2:
							value=(((uint16_t)instruction->d.define.data[j*instruction->d.define.membSize])<<8)|(instruction->d.define.data[j*instruction->d.define.membSize+1]);
						break;
						default: assert(false); break; // TODO: handle better
					}
					if (j>0)
						printf(", ");
					printf("%u", value);
					if (instruction->d.define.membSize==1 && isgraph(value))
						printf("=%c", value);
				}
				printf("] (%s:%u '%s')\n", line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeMov:
				printf("mov %s=%s (%s:%u '%s')\n", instruction->d.mov.dest, instruction->d.mov.src, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeLabel:
				printf("label %s (%s:%u '%s')\n", instruction->d.label.symbol, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeSyscall:
				printf("syscall (%s:%u '%s')\n", line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeClearInstructionCache:
				printf("clricache (%s:%u '%s')\n", line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeDebug:
				printf("debug (%s:%u '%s')\n", line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeAlu:
				switch(instruction->d.alu.type) {
					case BytecodeInstructionAluTypeInc:
						if (instruction->d.alu.incDecValue==1)
							printf("%s++ (%s:%u '%s')\n", instruction->d.alu.dest, line->file, line->lineNum, line->original);
						else
							printf("%s+=%u (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.incDecValue, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeDec:
						if (instruction->d.alu.incDecValue==1)
							printf("%s-- (%s:%u '%s')\n", instruction->d.alu.dest, line->file, line->lineNum, line->original);
						else
							printf("%s-=%u (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.incDecValue, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeAdd:
						printf("%s=%s+%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeSub:
						printf("%s=%s-%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeMul:
						printf("%s=%s*%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeDiv:
						printf("%s=%s/%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeXor:
						printf("%s=%s^%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeOr:
						printf("%s=%s|%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeAnd:
						printf("%s=%s&%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeCmp:
						printf("%s=cmp(%s,%s) (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeShiftLeft:
						printf("%s=%s<<%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeShiftRight:
						printf("%s=%s>>%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeSkip:
						printf("skip if %s has bit %u set (%s) (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.skipBit, byteCodeInstructionAluCmpBitStrings[instruction->d.alu.skipBit], line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeExtra:
						switch(instruction->d.alu.extraType) {
							case BytecodeInstructionAluExtraTypeNot:
								printf("%s=~%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->file, line->lineNum, line->original);
							break;
							case BytecodeInstructionAluExtraTypeStore16:
								printf("[%s]=%s (16 bit) (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->file, line->lineNum, line->original);
							break;
							case BytecodeInstructionAluExtraTypeLoad16:
								printf("%s=[%s] (16 bit) (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->file, line->lineNum, line->original);
							break;
							case BytecodeInstructionAluExtraTypePush16:
								printf("[r%u]=%s,r%u+=2 (16 bit push) (%s:%u '%s')\n", BytecodeRegisterSP, instruction->d.alu.dest, BytecodeRegisterSP, line->file, line->lineNum, line->original);
							break;
							case BytecodeInstructionAluExtraTypePop16:
								printf("r%u-=2,%s=[r%u] (16 bit pop) (%s:%u '%s')\n", BytecodeRegisterSP, instruction->d.alu.dest, BytecodeRegisterSP, line->file, line->lineNum, line->original);
							break;
							case BytecodeInstructionAluExtraTypeCall:
								// This should never be reached.
								// 'call label' is actually done as pseudo instruction,
								// and alu call is not supported.
								assert(false); // TODO: handle better
							break;
						}
					break;
				}
			break;
			case AssemblerInstructionTypeJmp:
				printf("jmp %s (%s:%u '%s')\n", instruction->d.jmp.addr, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypePush8:
				printf("push8 %s (%s:%u '%s')\n", instruction->d.push8.src, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypePop8:
				printf("pop8 %s (%s:%u '%s')\n", instruction->d.pop8.dest, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeCall:
				printf("call %s (%s:%u '%s')\n", instruction->d.call.label, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeRet:
				printf("ret (%s:%u '%s')\n", line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeStore8:
				printf("[%s]=%s (8 bit) (%s:%u '%s')\n", instruction->d.store8.dest, instruction->d.store8.src, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeLoad8:
				printf("%s=[%s] (8 bit) (%s:%u '%s')\n", instruction->d.load8.dest, instruction->d.load8.src, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeXchg8:
				printf("xchg8 *%s %s (%s:%u '%s')\n", instruction->d.xchg8.addrReg, instruction->d.xchg8.srcDestReg, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeConst:
				printf("const %s=%u (%s:%u '%s')\n", instruction->d.constSymbol.symbol, instruction->d.constSymbol.value, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypeNop:
				printf("nop (%s:%u '%s')\n", line->file, line->lineNum, line->original);
			break;
		}
	}
}

bool assemblerProgramWriteMachineCode(const AssemblerProgram *program, const char *path) {
	assert(program!=NULL);
	assert(path!=NULL);

	// Open file
	FILE *file=fopen(path, "w");
	if (file==NULL) {
		printf("Could not open output file '%s' for writing\n", path);
		goto error;
	}

	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *instruction=&program->instructions[i];
		if (fwrite(instruction->machineCode, 1, instruction->machineCodeLen, file)!=instruction->machineCodeLen)
			goto error;
	}

	fclose(file);
	return true;

	error:
	fclose(file);
	return false;
}

int assemblerGetAllocationSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	// Search through instructions looking for this symbol being allocated
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *loopInstruction=&program->instructions[i];
		if (loopInstruction->type==AssemblerInstructionTypeAllocation && strcmp(loopInstruction->d.allocation.symbol, symbol)==0)
			return i;
	}

	return -1;
}

int assemblerGetAllocationSymbolAddr(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	int index=assemblerGetAllocationSymbolInstructionIndex(program, symbol);
	if (index==-1)
		return -1;

	return program->instructions[index].d.allocation.ramOffset;
}

int assemblerGetDefineSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	// Search through instructions looking for this symbol being defined
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *loopInstruction=&program->instructions[i];
		if (loopInstruction->type==AssemblerInstructionTypeDefine && strcmp(loopInstruction->d.define.symbol, symbol)==0)
			return i;
	}

	return -1;
}

int assemblerGetDefineSymbolAddr(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	int index=assemblerGetDefineSymbolInstructionIndex(program, symbol);
	if (index==-1)
		return -1;

	// If we are not pointing into another define's data, then return our own address
	if (program->instructions[index].d.define.pointerLineIndex==program->instructions[index].lineIndex)
		return program->instructions[index].machineCodeOffset;

	// However if we are pointing into another define, return its address instead (with an offset)
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *loopInstruction=&program->instructions[i];
		if (loopInstruction->lineIndex==program->instructions[index].d.define.pointerLineIndex) {
			return loopInstruction->machineCodeOffset+program->instructions[index].d.define.pointerOffset;
		}
	}
	return -1; // bad pointer
}

int assemblerGetLabelSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	// Search through instructions looking for this symbol being defined
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *loopInstruction=&program->instructions[i];
		if (loopInstruction->type==AssemblerInstructionTypeLabel && strcmp(loopInstruction->d.label.symbol, symbol)==0)
			return i;
	}

	return -1;
}

int assemblerGetLabelSymbolAddr(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	int index=assemblerGetLabelSymbolInstructionIndex(program, symbol);
	if (index==-1)
		return -1;

	return program->instructions[index].machineCodeOffset;
}

int assemblerGetConstSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	// Search through instructions looking for this symbol being defined
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		const AssemblerInstruction *loopInstruction=&program->instructions[i];
		if (loopInstruction->type==AssemblerInstructionTypeConst && strcmp(loopInstruction->d.constSymbol.symbol, symbol)==0)
			return i;
	}

	return -1;
}

int assemblerGetConstSymbolValue(const AssemblerProgram *program, const char *symbol) {
	assert(program!=NULL);
	assert(symbol!=NULL);

	int index=assemblerGetConstSymbolInstructionIndex(program, symbol);
	if (index==-1)
		return -1;

	return program->instructions[index].d.constSymbol.value;
}

BytecodeRegister assemblerRegisterFromStr(const char *str) {
	if (str==NULL || str[0]!='r' || (str[1]<'0' || str[1]>'7') || str[2]!='\0')
		return BytecodeRegisterNB;
	else
		return str[1]-'0';
}

bool assemblerProgramAddIncludeDir(AssemblerProgram *program, const char *dir) {
	if (program->assemblerIncludeDirsNext>=AssemblerIncludeDirMax)
		return false;
	strcpy(program->assemblerIncludeDirs[program->assemblerIncludeDirsNext++], dir);
	return true;
}
