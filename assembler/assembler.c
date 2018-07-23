#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/bytecode.h"

#define AssemblerLinesMax 65536

typedef struct {
	BytecodeInstructionAluType type;
	const char *str;
	unsigned ops;
	unsigned skipBit;
} AssemblerInstructionAluData;

const AssemblerInstructionAluData assemblerInstructionAluData[]={
	{.type=BytecodeInstructionAluTypeInc, .str="inc", .ops=0},
	{.type=BytecodeInstructionAluTypeDec, .str="dec", .ops=0},
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
	{.type=BytecodeInstructionAluTypeSkip, .str="skipeq", .ops=1, .skipBit=BytecodeInstructionAluCmpBitEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipeqz", .ops=1, .skipBit=BytecodeInstructionAluCmpBitEqualZero},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipneq", .ops=1, .skipBit=BytecodeInstructionAluCmpBitNotEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipneqz", .ops=1, .skipBit=BytecodeInstructionAluCmpBitNotEqualZero},
	{.type=BytecodeInstructionAluTypeSkip, .str="skiplt", .ops=1, .skipBit=BytecodeInstructionAluCmpBitLessThan},
	{.type=BytecodeInstructionAluTypeSkip, .str="skiple", .ops=1, .skipBit=BytecodeInstructionAluCmpBitLessEqual},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipgt", .ops=1, .skipBit=BytecodeInstructionAluCmpBitGreaterThan},
	{.type=BytecodeInstructionAluTypeSkip, .str="skipge", .ops=1, .skipBit=BytecodeInstructionAluCmpBitGreaterEqual},
};

typedef enum {
	AssemblerInstructionTypeDefine,
	AssemblerInstructionTypeMov,
	AssemblerInstructionTypeLabel,
	AssemblerInstructionTypeSyscall,
	AssemblerInstructionTypeAlu,
	AssemblerInstructionTypeJmp,
} AssemblerInstructionType;

typedef struct {
	uint16_t membSize, len, totalSize; // for membSize: 1=byte, 2=word
	const char *symbol;
	uint8_t data[1024]; // TODO: this is pretty wasteful...
} AssemblerInstructionDefine;

typedef struct {
	const char *dest;
	const char *src;
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
} AssemblerInstructionAlu;

typedef struct {
	const char *addr;
} AssemblerInstructionJmp;

typedef struct {
	uint16_t lineIndex;
	char *modifiedLineCopy; // so we can have fields pointing into this
	AssemblerInstructionType type;
	union {
		AssemblerInstructionDefine define;
		AssemblerInstructionMov mov;
		AssemblerInstructionLabel label;
		AssemblerInstructionAlu alu;
		AssemblerInstructionJmp jmp;
	} d;

	uint8_t machineCode[1024]; // TODO: this is pretty wasteful...
	uint16_t machineCodeLen;
	uint16_t machineCodeOffset;
} AssemblerInstruction;

typedef struct {
	unsigned lineNum;
	char *original;
	char *modified;
} AssemblerLine;

typedef struct {
	AssemblerLine *lines[AssemblerLinesMax];
	size_t linesNext;

	AssemblerInstruction instructions[AssemblerLinesMax];
	size_t instructionsNext;
} AssemblerProgram;

int main(int argc, char **argv) {
	// Parse arguments
	if (argc!=3 && argc!=4) {
		printf("Usage: %s inputfile outputfile [--verbose]\n", argv[0]);
		return 1;
	}

	const char *inputPath=argv[1];
	const char *outputPath=argv[2];
	bool verbose=(argc==4 && strcmp(argv[3], "--verbose")==0);

	// Parse input file
	FILE *inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		return 1;
	}

	// Read line-by-line
	AssemblerProgram *program=malloc(sizeof(AssemblerProgram)); // TODO: Check return
	program->linesNext=0;
	program->instructionsNext=0;

	char *line=NULL;
	size_t lineSize=0;
	unsigned lineNum=0;
	while(getline(&line, &lineSize, inputFile)>0) {
		// Trim trailing newline
		if (line[strlen(line)-1]=='\n')
			line[strlen(line)-1]='\0';

		// Begin creating structure to represent this line
		AssemblerLine *assemblerLine=malloc(sizeof(AssemblerLine));
		program->lines[program->linesNext++]=assemblerLine;

		assemblerLine->lineNum=lineNum;
		assemblerLine->original=malloc(strlen(line)+1); // TODO: Check return
		strcpy(assemblerLine->original, line);
		assemblerLine->modified=malloc(strlen(line)+1); // TODO: Check return
		strcpy(assemblerLine->modified, line);

		// Advance to next line
		++lineNum;
	}
	free(line);

	fclose(inputFile);

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
					*c='\0';
					break;
				} else if (isspace(*c))
					*c=' ';
			}
		}

		// Replace two or more spaces with a single space (outside of strings)
		bool change;
		do {
			change=false;
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
							change=true;
						}
					}
				}
			}
		} while(change);

		// Trim preceeding or trailing white space.
		if (assemblerLine->modified[0]==' ')
			memmove(assemblerLine->modified, assemblerLine->modified+1, strlen(assemblerLine->modified+1)+1);
		if (strlen(assemblerLine->modified)>0 && assemblerLine->modified[strlen(assemblerLine->modified)-1]==' ')
			assemblerLine->modified[strlen(assemblerLine->modified)-1]='\0';
	}

	// Verbose output
	if (verbose) {
		printf("Non-blank input lines:\n");
		for(unsigned i=0; i<program->linesNext; ++i) {
			AssemblerLine *assemblerLine=program->lines[i];

			if (strlen(assemblerLine->modified)>0)
				printf("	%6u: '%s' -> '%s'\n", assemblerLine->lineNum, assemblerLine->original, assemblerLine->modified);
		}
	}

	// Parse lines
	for(unsigned i=0; i<program->linesNext; ++i) {
		AssemblerLine *assemblerLine=program->lines[i];

		// Skip empty lines
		if (strlen(assemblerLine->modified)==0)
			continue;

		// Parse operation
		char *lineCopy=malloc(strlen(assemblerLine->modified)+1); // TODO: Check
		strcpy(lineCopy, assemblerLine->modified);

		char *savePtr;
		char *first=strtok_r(lineCopy, " ", &savePtr);
		if (first==NULL) {
			free(lineCopy);
			continue;
		}

		if (strcmp(first, "db")==0 || strcmp(first, "dw")==0) {
			unsigned membSize=0;
			switch(first[1]) {
				case 'b': membSize=1; break;
				case 'w': membSize=2; break;
			}
			assert(membSize!=0);

			char *symbol=strtok_r(NULL, " ", &savePtr);
			if (symbol==NULL) {
				printf("error - expected symbol name after '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
				goto done;
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
				printf("error - expected dest after '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
				goto done;
			}
			char *src=strtok_r(NULL, " ", &savePtr);
			if (src==NULL) {
				printf("error - expected src after '%s' (%u:'%s')\n", dest, assemblerLine->lineNum, assemblerLine->original);
				goto done;
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
				printf("error - expected symbol after '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
				goto done;
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
				printf("error - expected address label after '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
				goto done;
			}

			AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
			instruction->lineIndex=i;
			instruction->modifiedLineCopy=lineCopy;
			instruction->type=AssemblerInstructionTypeJmp;
			instruction->d.jmp.addr=addr;
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
					printf("error - expected dest after '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
					goto done;
				}

				char *opA=NULL, *opB=NULL;

				if (assemblerInstructionAluData[j].ops>=1) {
					opA=strtok_r(NULL, " ", &savePtr);
					if (opA==NULL) {
						printf("error - expected operand A after '%s' (%u:'%s')\n", dest, assemblerLine->lineNum, assemblerLine->original);
						goto done;
					}
				}

				if (assemblerInstructionAluData[j].ops>=2) {
					opB=strtok_r(NULL, " ", &savePtr);
					if (opB==NULL) {
						printf("error - expected operand B after '%s' (%u:'%s')\n", opA, assemblerLine->lineNum, assemblerLine->original);
						goto done;
					}
				}

				AssemblerInstruction *instruction=&program->instructions[program->instructionsNext++];
				instruction->lineIndex=i;
				instruction->modifiedLineCopy=lineCopy;
				instruction->type=AssemblerInstructionTypeAlu;
				instruction->d.alu.type=assemblerInstructionAluData[j].type;
				if (instruction->d.alu.type==BytecodeInstructionAluTypeSkip)
					instruction->d.alu.skipBit=assemblerInstructionAluData[j].skipBit;
				instruction->d.alu.dest=dest;
				instruction->d.alu.opA=opA;
				instruction->d.alu.opB=opB;
			} else {
				printf("error - unknown/unimplemented instruction '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
				free(lineCopy);
				goto done;
			}
		}
	}

	// Move defines to be after everything else
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
	} while(change);

	// Generate machine code for each instruction
	for(unsigned genPass=0; genPass<3; ++genPass) {
		// Passes:
		// 0 - generates intitial machine code with placeholders
		// 1 - computes offsets for all instructions in the final executable
		// 2 - fills in symbol and label addresses computed in previous step
		unsigned nextMachineCodeOffset=0;
		for(unsigned i=0; i<program->instructionsNext; ++i) {
			AssemblerInstruction *instruction=&program->instructions[i];
			AssemblerLine *line=program->lines[instruction->lineIndex];

			if (genPass==1) {
				instruction->machineCodeOffset=nextMachineCodeOffset;
				nextMachineCodeOffset+=instruction->machineCodeLen;
				continue;
			}

			switch(instruction->type) {
				case AssemblerInstructionTypeDefine:
					switch(genPass) {
						case 0:
							// Simply copy data into program memory directly
							for(unsigned j=0; j<instruction->d.define.totalSize; ++j)
								instruction->machineCode[instruction->machineCodeLen++]=instruction->d.define.data[j];
						break;
					}
				break;
				case AssemblerInstructionTypeMov:
					switch(genPass) {
						case 0:
							// Reserve three bytes
							instruction->machineCodeLen=3;
						break;
						case 2: {
							// Verify dest is a valid register
							if (instruction->d.mov.dest[0]!='r' || (instruction->d.mov.dest[1]<'0' || instruction->d.mov.dest[1]>'7') || instruction->d.mov.dest[2]!='\0') {
								printf("error - expected register (r0-r7) as destination, instead got '%s' (%u:'%s')\n", instruction->d.mov.dest, line->lineNum, line->original);
								goto done;
							}

							BytecodeRegister destReg=(instruction->d.mov.dest[1]-'0');

							// Determine type of src
							if (isdigit(instruction->d.mov.src[0])) {
								// Integer - use set16 instruction
								unsigned value=atoi(instruction->d.mov.src);
								bytecodeInstructionCreateMiscSet16(instruction->machineCode, destReg, value);
							} else if (instruction->d.mov.src[0]=='r' && (instruction->d.mov.src[1]>='0' && instruction->d.mov.src[1]<='7') && instruction->d.mov.src[2]=='\0') {
								// Register - use dest=src|src as a copy
								// also add a nop to pad to 3 bytes
								BytecodeRegister srcReg=instruction->d.mov.src[1]-'0';
								BytecodeInstructionStandard copyOp=bytecodeInstructionCreateAlu(BytecodeInstructionAluTypeOr, destReg, srcReg, srcReg);

								instruction->machineCode[0]=(copyOp>>8);
								instruction->machineCode[1]=(copyOp&0xFF);
								instruction->machineCode[2]=bytecodeInstructionCreateMiscNop();
							} else if (instruction->d.mov.src[0]=='_' || isalnum(instruction->d.mov.src[0])) {
								// Symbol

								// Search through instructions looking for this symbol being defined
								unsigned addr, k;
								for(k=0; k<program->instructionsNext; ++k) {
									AssemblerInstruction *loopInstruction=&program->instructions[k];
									if (loopInstruction->type==AssemblerInstructionTypeDefine && strcmp(loopInstruction->d.define.symbol, instruction->d.mov.src)==0) {
										addr=loopInstruction->machineCodeOffset;
										break;
									}
								}
								if (k==program->instructionsNext) {
									printf("error - bad src '%s' (%u:'%s')\n", instruction->d.mov.src, line->lineNum, line->original);
									goto done;
								}

								// Create instruction using the found address
								bytecodeInstructionCreateMiscSet16(instruction->machineCode, destReg, addr);
							} else {
								printf("error - bad src '%s' (%u:'%s')\n", instruction->d.mov.src, line->lineNum, line->original);
								goto done;
							}
						} break;
					}
				break;
				case AssemblerInstructionTypeLabel:
					switch(genPass) {
						case 0:
							instruction->machineCodeLen=0;
						break;
					}
				break;
				case AssemblerInstructionTypeSyscall:
					switch(genPass) {
						case 0:
							instruction->machineCode[0]=bytecodeInstructionCreateMiscSyscall();
							instruction->machineCodeLen=1;
						break;
					}
				break;
				case AssemblerInstructionTypeAlu:
					switch(genPass) {
						case 0:
							// Reserve two bytes
							instruction->machineCodeLen=2;
						break;
						case 2: {
							// Verify dest is a valid register
							if (instruction->d.alu.dest[0]!='r' || (instruction->d.alu.dest[1]<'0' || instruction->d.alu.dest[1]>'7') || instruction->d.alu.dest[2]!='\0') {
								printf("error - expected register (r0-r7) as destination, instead got '%s' (%u:'%s')\n", instruction->d.alu.dest, line->lineNum, line->original);
								goto done;
							}

							BytecodeRegister destReg=(instruction->d.alu.dest[1]-'0');

							// Read operand registers
							// TODO: This better (i.e. check how many we expect and error if not enough)
							BytecodeRegister opAReg=BytecodeRegister0;
							BytecodeRegister opBReg=BytecodeRegister0;

							if (instruction->d.alu.opA!=NULL && instruction->d.alu.opA[0]=='r' && (instruction->d.alu.opA[1]>='0' && instruction->d.alu.opA[1]<='7') && instruction->d.alu.opA[2]=='\0')
								opAReg=(instruction->d.alu.opA[1]-'0');

							if (instruction->d.alu.opB!=NULL && instruction->d.alu.opB[0]=='r' && (instruction->d.alu.opB[1]>='0' && instruction->d.alu.opB[1]<='7') && instruction->d.alu.opB[2]=='\0')
								opBReg=(instruction->d.alu.opB[1]-'0');

							// Create instruction
							if (instruction->d.alu.type==BytecodeInstructionAluTypeSkip)
								// Special case to encode literal bit index
								opAReg=instruction->d.alu.skipBit;
							BytecodeInstructionStandard aluOp=bytecodeInstructionCreateAlu(instruction->d.alu.type, destReg, opAReg, opBReg);
							instruction->machineCode[0]=(aluOp>>8);
							instruction->machineCode[1]=(aluOp&0xFF);
						} break;
					}
				break;
				case AssemblerInstructionTypeJmp:
					switch(genPass) {
						case 0:
							// Reserve three bytes for set16 instruction
							instruction->machineCodeLen=3;
						break;
						case 2: {
							// Search through instructions looking for the label being defined
							unsigned addr, k;
							for(k=0; k<program->instructionsNext; ++k) {
								AssemblerInstruction *loopInstruction=&program->instructions[k];
								if (loopInstruction->type==AssemblerInstructionTypeLabel && strcmp(loopInstruction->d.label.symbol, instruction->d.jmp.addr)==0) {
									addr=loopInstruction->machineCodeOffset;
									break;
								}
							}
							if (k==program->instructionsNext) {
								printf("error - bad jump label '%s' (%u:'%s')\n", instruction->d.mov.src, line->lineNum, line->original);
								goto done;
							}

							// Create instruction which sets the IP register
							bytecodeInstructionCreateMiscSet16(instruction->machineCode, ByteCodeRegisterIP, addr);
						} break;
					}
				break;
			}
		}
	}

	// Verbose output
	if (verbose) {
		printf("Instructions:\n");
		for(unsigned i=0; i<program->instructionsNext; ++i) {
			AssemblerInstruction *instruction=&program->instructions[i];
			AssemblerLine *line=program->lines[instruction->lineIndex];

			printf("	%6u 0x%04X ", i, instruction->machineCodeOffset);
			for(unsigned j=0; j<instruction->machineCodeLen; ++j)
				printf("%02X", instruction->machineCode[j]);
			printf(": ");

			switch(instruction->type) {
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
					printf("] (%u:'%s')\n", line->lineNum, line->original);
				break;
				case AssemblerInstructionTypeMov:
					printf("mov %s=%s (%u:'%s')\n", instruction->d.mov.dest, instruction->d.mov.src, line->lineNum, line->original);
				break;
				case AssemblerInstructionTypeLabel:
					printf("label %s (%u:'%s')\n", instruction->d.label.symbol, line->lineNum, line->original);
				break;
				case AssemblerInstructionTypeSyscall:
					printf("syscall (%u:'%s')\n", line->lineNum, line->original);
				break;
				case AssemblerInstructionTypeAlu:
					switch(instruction->d.alu.type) {
						case BytecodeInstructionAluTypeInc:
							printf("%s++ (%u:'%s')\n", instruction->d.alu.dest, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeDec:
							printf("%s-- (%u:'%s')\n", instruction->d.alu.dest, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeAdd:
							printf("%s=%s+%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeSub:
							printf("%s=%s-%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeMul:
							printf("%s=%s*%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeDiv:
							printf("%s=%s/%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeXor:
							printf("%s=%s^%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeOr:
							printf("%s=%s|%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeAnd:
							printf("%s=%s&%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeNot:
							printf("%s=~%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeCmp:
							printf("%s=cmp(%s,%s) (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeShiftLeft:
							printf("%s=%s<<%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeShiftRight:
							printf("%s=%s>>%s (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.opA, instruction->d.alu.opB, line->lineNum, line->original);
						break;
						case BytecodeInstructionAluTypeSkip: {
							printf("skip if %s has bit %u set (%s) (%u:'%s')\n", instruction->d.alu.dest, instruction->d.alu.skipBit, byteCodeInstructionAluCmpBitStrings[instruction->d.alu.skipBit], line->lineNum, line->original);
						}
						break;
					}
				break;
				case AssemblerInstructionTypeJmp:
					printf("jmp %s (%u:'%s')\n", instruction->d.jmp.addr, line->lineNum, line->original);
				break;
			}
		}
	}

	// Output machine code
	FILE *outputFile=fopen(outputPath, "w");
	if (outputFile==NULL) {
		printf("Could not open output file '%s' for writing\n", outputPath);
		goto done;
	}

	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		fwrite(instruction->machineCode, 1, instruction->machineCodeLen, outputFile); // TODO: Check return
	}

	fclose(outputFile);

	// Tidy up
	done:
	for(unsigned i=0; i<program->linesNext; ++i) {
		AssemblerLine *assemblerLine=program->lines[i];
		free(assemblerLine->original);
		free(assemblerLine->modified);
		free(assemblerLine);
	}
	for(unsigned i=0; i<program->instructionsNext; ++i) {
		AssemblerInstruction *instruction=&program->instructions[i];
		free(instruction->modifiedLineCopy);
	}

	return 0;
}
