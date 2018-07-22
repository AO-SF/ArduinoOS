#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AssemblerLinesMax 65536

typedef enum {
	AssemblerInstructionTypeDefine,
} AssemblerInstructionType;

typedef struct {
	uint16_t membSize, len, totalSize; // for membSize: 1=byte, 2=word
	const char *symbol;
	uint8_t data[65536]; // TODO: this is pretty wasteful...
} AssemblerInstructionDefine;

typedef struct {
	uint16_t lineIndex;
	char *modifiedLineCopy; // so we can have fields pointing into this
	AssemblerInstructionType type;
	union {
		AssemblerInstructionDefine define;
	} d;
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

	// Output
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
		} else {
			printf("error - unknown/unimplemented instruction '%s' (%u:'%s')\n", first, assemblerLine->lineNum, assemblerLine->original);
			free(lineCopy);
			// goto done; TODO: put this back once we have implemented a few more instructions
		}
	}

	// Output
	if (verbose) {
		printf("Instructions:\n");
		for(unsigned i=0; i<program->instructionsNext; ++i) {
			AssemblerInstruction *instruction=&program->instructions[i];
			AssemblerLine *line=program->lines[instruction->lineIndex];

			switch(instruction->type) {
				case AssemblerInstructionTypeDefine:
					printf("	%6u: define membSize=%u, len=%u, totalSize=%u, symbol=%s data=[", i, instruction->d.define.membSize, instruction->d.define.len, instruction->d.define.totalSize, instruction->d.define.symbol);
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
			}
		}
	}

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
