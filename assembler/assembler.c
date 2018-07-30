#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/bytecode.h"
#include "../kernel/kernelfs.h"

#define AssemblerLinesMax 65536

typedef struct {
	BytecodeInstructionAluType type;
	const char *str;
	unsigned ops;
	unsigned skipBit;
	unsigned incDecValue;
} AssemblerInstructionAluData;

const AssemblerInstructionAluData assemblerInstructionAluData[]={
	{.type=BytecodeInstructionAluTypeInc, .str="inc", .ops=0, .incDecValue=1},
	{.type=BytecodeInstructionAluTypeInc, .str="inc2", .ops=0, .incDecValue=2},
	{.type=BytecodeInstructionAluTypeDec, .str="dec", .ops=0, .incDecValue=1},
	{.type=BytecodeInstructionAluTypeDec, .str="dec2", .ops=0, .incDecValue=2},
	{.type=BytecodeInstructionAluTypeAdd, .str="add", .ops=2},
	{.type=BytecodeInstructionAluTypeSub, .str="sub", .ops=2},
	{.type=BytecodeInstructionAluTypeMul, .str="mul", .ops=2},
	{.type=BytecodeInstructionAluTypeDiv, .str="div", .ops=2},
	{.type=BytecodeInstructionAluTypeXor, .str="xor", .ops=2},
	{.type=BytecodeInstructionAluTypeOr, .str="or", .ops=2},
	{.type=BytecodeInstructionAluTypeAnd, .str="and", .ops=2},
	{.type=BytecodeInstructionAluTypeNot, .str="not", .ops=1},
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
	{.type=BytecodeInstructionAluTypeStore16, .str="store16", .ops=1},
	{.type=BytecodeInstructionAluTypeLoad16, .str="load16", .ops=1},
};

typedef enum {
	AssemblerInstructionTypeAllocation,
	AssemblerInstructionTypeDefine,
	AssemblerInstructionTypeMov,
	AssemblerInstructionTypeLabel,
	AssemblerInstructionTypeSyscall,
	AssemblerInstructionTypeAlu,
	AssemblerInstructionTypeJmp,
	AssemblerInstructionTypePush,
	AssemblerInstructionTypePop,
	AssemblerInstructionTypeCall,
	AssemblerInstructionTypeRet,
	AssemblerInstructionTypeStore8,
	AssemblerInstructionTypeLoad8,
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
} AssemblerInstructionAlu;

typedef struct {
	const char *addr;
} AssemblerInstructionJmp;

typedef struct {
	const char *src;
} AssemblerInstructionPush;

typedef struct {
	const char *dest;
} AssemblerInstructionPop;

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
		AssemblerInstructionPush push;
		AssemblerInstructionPop pop;
		AssemblerInstructionCall call;
		AssemblerInstructionStore8 store8;
		AssemblerInstructionLoad8 load8;
	} d;

	uint8_t machineCode[1024]; // TODO: this is pretty wasteful...
	uint16_t machineCodeLen;
	uint16_t machineCodeOffset;
} AssemblerInstruction;

typedef struct {
	char *file;
	unsigned lineNum;
	char *original;
	char *modified;
} AssemblerLine;

typedef struct {
	AssemblerLine *lines[AssemblerLinesMax];
	size_t linesNext;

	AssemblerInstruction instructions[AssemblerLinesMax];
	size_t instructionsNext;

	uint16_t stackRamOffset;

	char includedPaths[256][1024]; // TODO: Avoid hardcoded limits (or at least check them...)
	size_t includePathsNext;

	bool noStack, noScratch;
} AssemblerProgram;

AssemblerProgram *assemblerProgramNew(void);
void assemblerProgramFree(AssemblerProgram *program);

void assemblerInsertLine(AssemblerProgram *program, AssemblerLine *line, int offset);
bool assemblerInsertLinesFromFile(AssemblerProgram *program, const char *path, int offset);
void assemblerRemoveLine(AssemblerProgram *program, int offset);

bool assemblerProgramPreprocess(AssemblerProgram *program); // strips comments, whitespace etc. returns true if any changes made
bool assemblerProgramHandleNextInclude(AssemblerProgram *program, bool *change); // returns false on failure
bool assemblerProgramHandleNextOption(AssemblerProgram *program, bool *change); // returns false on failure

bool assemblerProgramParseLines(AssemblerProgram *program); // converts lines to initial instructions, returns false on error

bool assemblerProgramShiftDefines(AssemblerProgram *program); // moves defines to the end to avoid getting in the way of code, returns true if any changes made

bool assemblerProgramGenerateInitialMachineCode(AssemblerProgram *program); // returns false on failure
void assemblerProgramComputeMachineCodeOffsets(AssemblerProgram *program);
bool assemblerProgramComputeFinalMachineCode(AssemblerProgram *program); // returns false on failure

void assemblerProgramDebugInstructions(const AssemblerProgram *program);

bool assemblerProgramWriteMachineCode(const AssemblerProgram *program, const char *path); // returns false on failure

int assemblerGetAllocationSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetAllocationSymbolAddr(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise result points into RAM memory

int assemblerGetDefineSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetDefineSymbolAddr(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise result points into read-only program memory

int assemblerGetLabelSymbolInstructionIndex(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found
int assemblerGetLabelSymbolAddr(const AssemblerProgram *program, const char *symbol); // Returns -1 if symbol not found, otherwise result points into read-only program memory

BytecodeRegister assemblerRegisterFromStr(const char *str); // Returns BytecodeRegisterNB on failure

int main(int argc, char **argv) {
	// Parse arguments
	if (argc!=3 && argc!=4) {
		printf("Usage: %s inputfile outputfile [--verbose]\n", argv[0]);
		return 1;
	}

	const char *inputPath=argv[1];
	const char *outputPath=argv[2];
	bool verbose=(argc==4 && strcmp(argv[3], "--verbose")==0);

	// Create program struct
	AssemblerProgram *program=assemblerProgramNew();
	if (program==NULL)
		goto done;

	// Read input file line-by-line
	if (!assemblerInsertLinesFromFile(program, inputPath, 0))
		goto done;

	// Preprocess (handle whitespace, includes etc)
	bool change;
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

	// Unless nostack set, add line to set the stack pointer (this is just reserving it for now)
	if (!program->noStack) {
		const char *spFile="<auto>";
		char spLine[64];
		sprintf(spLine, "mov r%u 65535", ByteCodeRegisterSP);

		AssemblerLine *assemblerLine=malloc(sizeof(AssemblerLine)); // TODO: Check return
		assemblerLine->lineNum=1;
		assemblerLine->file=malloc(strlen(spFile)+1); // TODO: Check return
		strcpy(assemblerLine->file, spFile);
		assemblerLine->original=malloc(strlen(spLine)+1); // TODO: Check return
		strcpy(assemblerLine->original, spLine);
		assemblerLine->modified=malloc(strlen(spLine)+1); // TODO: Check return
		strcpy(assemblerLine->modified, spLine);

		assemblerInsertLine(program, assemblerLine, 0);
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

	// Generate machine code for each instruction
	if (!assemblerProgramGenerateInitialMachineCode(program))
		goto done;

	assemblerProgramComputeMachineCodeOffsets(program);

	// Update instruction we created earlier (if we did) to set the stack pointer register (now that we have computed offsets)
	if (!program->noStack) {
		// TODO: Check for not finding
		for(unsigned i=0; i<program->instructionsNext; ++i) {
			AssemblerInstruction *instruction=&program->instructions[i];
			if (instruction->lineIndex==0) {
				// Update line to put correct ram offset in. No need to update dest or src pointers as the start of the string has not changed, and we know this is safe because the new offset cannot be longer than the one we used when allocating the line in the first place.
				sprintf(instruction->d.mov.src, "%u", program->stackRamOffset);
				break;
			}
		}
	}

	if (!assemblerProgramComputeFinalMachineCode(program))
		goto done;

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

bool assemblerInsertLinesFromFile(AssemblerProgram *program, const char *gPath, int offset) {
	assert(program!=NULL);
	assert(gPath!=NULL);

	// Normalise path
	char path[1024]; // TODO: better
	strcpy(path, gPath);
	kernelFsPathNormalise(path);

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
		AssemblerLine *assemblerLine=malloc(sizeof(AssemblerLine)); // TODO: Check return

		assemblerLine->lineNum=lineNum;
		assemblerLine->file=malloc(strlen(path)+1); // TODO: Check return
		strcpy(assemblerLine->file, path);
		assemblerLine->original=malloc(strlen(line)+1); // TODO: Check return
		strcpy(assemblerLine->original, line);
		assemblerLine->modified=malloc(strlen(line)+1); // TODO: Check return
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
		strcpy(newPath, assemblerLine->file);

		char *lastSlash=strrchr(newPath, '/');
		if (lastSlash!=NULL)
			lastSlash[1]='\0';
		else
			newPath[0]='\0';

		const char *relPath=strchr(assemblerLine->modified, ' ')+1; // No need to check return as we proved there was a space above
		strcat(newPath, relPath);

		kernelFsPathNormalise(newPath);

		// Remove this line
		assemblerRemoveLine(program, line);

		// Indicate a change has occured
		if (change!=NULL)
			*change=true;

		// If using require rather than include, and this file has already been included/required, then skip
		if (isRequire) {
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
		char *lineCopy=malloc(strlen(assemblerLine->modified)+1); // TODO: Check return
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

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeAllocation;
			instruction->d.allocation.membSize=membSize;
			instruction->d.allocation.len=atoi(lenStr);
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
							unsigned value=atoi(tempInteger);
							switch(instruction->d.define.membSize) {
								case 1:
									instruction->d.define.data[instruction->d.define.len++]=value;
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
					} else if (isdigit(*dataChar)) {
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
				unsigned value=atoi(tempInteger);
				switch(instruction->d.define.membSize) {
					case 1:
						instruction->d.define.data[instruction->d.define.len++]=value;
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
		} else if (strcmp(first, "push")==0) {
			char *src=strtok_r(NULL, " ", &savePtr);
			if (src==NULL) {
				printf("error - expected src register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypePush;
			instruction->d.push.src=src;
		} else if (strcmp(first, "pop")==0) {
			char *dest=strtok_r(NULL, " ", &savePtr);
			if (dest==NULL) {
				printf("error - expected dest register after '%s' (%s:%u '%s')\n", first, assemblerLine->file, assemblerLine->lineNum, assemblerLine->original);
				return false;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypePop;
			instruction->d.pop.dest=dest;
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

bool assemblerProgramGenerateInitialMachineCode(AssemblerProgram *program) {
	assert(program!=NULL);

	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		AssemblerLine *line=program->lines[instruction->lineIndex];

		switch(instruction->type) {
			case AssemblerInstructionTypeAllocation:
				instruction->machineCodeLen=0; // These are reserved in RAM not program memory
			break;
			case AssemblerInstructionTypeDefine:
				// Simply copy data into program memory directly
				for(unsigned j=0; j<instruction->d.define.totalSize; ++j)
					instruction->machineCode[instruction->machineCodeLen++]=instruction->d.define.data[j];
			break;
			case AssemblerInstructionTypeMov: {
				// Check src and determine type
				BytecodeRegister srcReg;
				int defineAddr, allocationAddr;
				if (isdigit(instruction->d.mov.src[0])) {
					// Integer - use set8 or set16 instruction as needed
					unsigned value=atoi(instruction->d.mov.src);
					if (value<256)
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
				} else {
					printf("error - bad src '%s' (%s:%u '%s')\n", instruction->d.mov.src, line->file, line->lineNum, line->original);
					return false;
				}
			} break;
			case AssemblerInstructionTypeLabel:
				instruction->machineCodeLen=0;
			break;
			case AssemblerInstructionTypeSyscall:
				instruction->machineCode[0]=bytecodeInstructionCreateMiscSyscall();
				instruction->machineCodeLen=1;
			break;
			case AssemblerInstructionTypeAlu:
				instruction->machineCodeLen=2; // all ALU instructions take 2 bytes
			break;
			case AssemblerInstructionTypeJmp:
				instruction->machineCodeLen=3; // Reserve three bytes for set16 instruction
			break;
			case AssemblerInstructionTypePush:
				instruction->machineCodeLen=4; // Reserve four bytes (store16 + inc2)
			break;
			case AssemblerInstructionTypePop:
				instruction->machineCodeLen=4; // Reserve four bytes (dec2 + load16)
			break;
			case AssemblerInstructionTypeCall:
				instruction->machineCodeLen=7; // Reserve seven bytes (store16, inc2, set16)
			break;
			case AssemblerInstructionTypeRet:
				instruction->machineCodeLen=8; // Reserve eight bytes (dec2, load16, inc5, or)
			break;
			case AssemblerInstructionTypeStore8:
				instruction->machineCodeLen=1;
			break;
			case AssemblerInstructionTypeLoad8:
				instruction->machineCodeLen=1;
			break;
		}
	}

	return true;
}

void assemblerProgramComputeMachineCodeOffsets(AssemblerProgram *program) {
	assert(program!=NULL);

	unsigned nextMachineCodeOffset=ByteCodeMemoryProgmemAddr;
	unsigned nextRamOffset=ByteCodeMemoryRamAddr;
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

bool assemblerProgramComputeFinalMachineCode(AssemblerProgram *program) {
	assert(program!=NULL);

	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		AssemblerLine *line=program->lines[instruction->lineIndex];

		switch(instruction->type) {
			case AssemblerInstructionTypeAllocation:
			break;
			case AssemblerInstructionTypeDefine:
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
				int defineAddr, allocationAddr;
				if (isdigit(instruction->d.mov.src[0])) {
					// Integer - use set8 or set16 instruction as needed
					unsigned value=atoi(instruction->d.mov.src);
					if (value<256) {
						BytecodeInstructionStandard set8Op=bytecodeInstructionCreateMiscSet8(destReg, value);
						instruction->machineCode[0]=(set8Op>>8);
						instruction->machineCode[1]=(set8Op&0xFF);
					} else
						bytecodeInstructionCreateMiscSet16(instruction->machineCode, destReg, value);
				} else if ((srcReg=assemblerRegisterFromStr(instruction->d.mov.src))!=BytecodeRegisterNB) {
					// Register - use dest=src|src as a copy, and add a nop to pad to 3 bytes
					BytecodeInstructionStandard copyOp=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeOr, destReg, srcReg, srcReg);
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

					BytecodeInstructionStandard set8Op=bytecodeInstructionCreateMiscSet8(destReg, c);
					instruction->machineCode[0]=(set8Op>>8);
					instruction->machineCode[1]=(set8Op&0xFF);
				} else if ((defineAddr=assemblerGetDefineSymbolAddr(program, instruction->d.mov.src))!=-1) {
					// Define symbol
					bytecodeInstructionCreateMiscSet16(instruction->machineCode, destReg, defineAddr);
				} else if ((allocationAddr=assemblerGetAllocationSymbolAddr(program, instruction->d.mov.src))!=-1) {
					bytecodeInstructionCreateMiscSet16(instruction->machineCode, destReg, allocationAddr);
				} else {
					printf("error - bad src '%s' (%s:%u '%s')\n", instruction->d.mov.src, line->file, line->lineNum, line->original);
					return false;
				}
			} break;
			case AssemblerInstructionTypeLabel:
			break;
			case AssemblerInstructionTypeSyscall:
			break;
			case AssemblerInstructionTypeAlu: {
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
				if (instruction->d.alu.type==BytecodeInstructionAluTypeSkip)
					// Special case to encode literal bit index
					opAReg=instruction->d.alu.skipBit;
				if (instruction->d.alu.type==BytecodeInstructionAluTypeInc || instruction->d.alu.type==BytecodeInstructionAluTypeDec) {
					// Special case to encode literal add/sub delta
					opAReg=(instruction->d.alu.incDecValue-1)>>3;
					opBReg=(instruction->d.alu.incDecValue-1)&7;
				}
				BytecodeInstructionStandard aluOp=bytecodeInstructionCreateAlu(instruction->d.alu.type, destReg, opAReg, opBReg);

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

				// Create instruction which sets the IP register
				bytecodeInstructionCreateMiscSet16(instruction->machineCode, ByteCodeRegisterIP, addr);
			} break;
			case AssemblerInstructionTypePush: {
				// This requires the stack register - can if we cannot use it
				if (program->noStack) {
					printf("error - push requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Verify src is a valid register
				BytecodeRegister srcReg=assemblerRegisterFromStr(instruction->d.push.src);
				if (srcReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as src, instead got '%s' (%s:%u '%s')\n", instruction->d.push.src, line->file, line->lineNum, line->original);
					return false;
				}

				// Create as two instructions: store16 SP srcReg; inc2 SP
				BytecodeInstructionStandard store16Op=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeStore16, ByteCodeRegisterSP, srcReg, 0);
				instruction->machineCode[0]=(store16Op>>8);
				instruction->machineCode[1]=(store16Op&0xFF);

				BytecodeInstructionStandard inc2Op=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeInc, ByteCodeRegisterSP, 2);
				instruction->machineCode[2]=(inc2Op>>8);
				instruction->machineCode[3]=(inc2Op&0xFF);
			} break;
			case AssemblerInstructionTypePop: {
				// This requires the stack register - can if we cannot use it
				if (program->noStack) {
					printf("error - pop requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Verify dest is a valid register
				BytecodeRegister destReg=assemblerRegisterFromStr(instruction->d.pop.dest);
				if (destReg==BytecodeRegisterNB) {
					printf("error - expected register (r0-r7) as dest, instead got '%s' (%s:%u '%s')\n", instruction->d.pop.dest, line->file, line->lineNum, line->original);
					return false;
				}

				// Create as two instructions: dec2 SP; load16 destReg SP
				BytecodeInstructionStandard dec2Op=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeDec, ByteCodeRegisterSP, 2);
				instruction->machineCode[0]=(dec2Op>>8);
				instruction->machineCode[1]=(dec2Op&0xFF);

				BytecodeInstructionStandard loadOp=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeLoad16, destReg, ByteCodeRegisterSP, 0);
				instruction->machineCode[2]=(loadOp>>8);
				instruction->machineCode[3]=(loadOp&0xFF);
			} break;
			case AssemblerInstructionTypeCall: {
				// This requires the stack register - can if we cannot use it
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

				// Create instructions (push IP onto stack and jump into function)
				BytecodeInstructionStandard store16Op=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeStore16, ByteCodeRegisterSP, ByteCodeRegisterIP, 0);
				instruction->machineCode[0]=(store16Op>>8);
				instruction->machineCode[1]=(store16Op&0xFF);

				BytecodeInstructionStandard incOp=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeInc, ByteCodeRegisterSP, 2);
				instruction->machineCode[2]=(incOp>>8);
				instruction->machineCode[3]=(incOp&0xFF);

				bytecodeInstructionCreateMiscSet16(instruction->machineCode+4, ByteCodeRegisterIP, addr);
			} break;
			case AssemblerInstructionTypeRet: {
				// This requires the scratch register - can if we cannot use it
				if (program->noScratch) {
					printf("error - ret requires scratch register but noscratch set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// This requires the stack register - can if we cannot use it
				if (program->noStack) {
					printf("error - ret requires stack register but nostack set (%s:%u '%s')\n", line->file, line->lineNum, line->original);
					return false;
				}

				// Create instructions (pop ret addr off stack, adjust it to skip call pseudo instructions, and then jump)
				BytecodeInstructionStandard decOp=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeDec, ByteCodeRegisterSP, 2);
				instruction->machineCode[0]=(decOp>>8);
				instruction->machineCode[1]=(decOp&0xFF);

				BytecodeInstructionStandard load16Op=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeLoad16, ByteCodeRegisterS, ByteCodeRegisterSP, 0);
				instruction->machineCode[2]=(load16Op>>8);
				instruction->machineCode[3]=(load16Op&0xFF);

				BytecodeInstructionStandard incOp=bytecodeInstructionCreateAluIncDecValue(BytecodeInstructionAluTypeInc, ByteCodeRegisterS, 5);
				instruction->machineCode[4]=(incOp>>8);
				instruction->machineCode[5]=(incOp&0xFF);

				BytecodeInstructionStandard setIpOp=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeOr, ByteCodeRegisterIP, ByteCodeRegisterS, ByteCodeRegisterS);
				instruction->machineCode[6]=(setIpOp>>8);
				instruction->machineCode[7]=(setIpOp&0xFF);
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
		}
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
					case BytecodeInstructionAluTypeNot:
						printf("%s=~%s (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->file, line->lineNum, line->original);
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
					case BytecodeInstructionAluTypeStore16:
						printf("[%s]=%s (16 bit) (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->file, line->lineNum, line->original);
					break;
					case BytecodeInstructionAluTypeLoad16:
						printf("%s=[%s] (16 bit) (%s:%u '%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->file, line->lineNum, line->original);
					break;
				}
			break;
			case AssemblerInstructionTypeJmp:
				printf("jmp %s (%s:%u '%s')\n", instruction->d.jmp.addr, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypePush:
				printf("push %s (%s:%u '%s')\n", instruction->d.push.src, line->file, line->lineNum, line->original);
			break;
			case AssemblerInstructionTypePop:
				printf("pop %s (%s:%u '%s')\n", instruction->d.pop.dest, line->file, line->lineNum, line->original);
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

	return program->instructions[index].machineCodeOffset;
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

BytecodeRegister assemblerRegisterFromStr(const char *str) {
	if (str==NULL || str[0]!='r' || (str[1]<'0' || str[1]>'7') || str[2]!='\0')
		return BytecodeRegisterNB;
	else
		return str[1]-'0';
}
