#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AssemblerLinesMax 65536

typedef struct {
	unsigned lineNum;
	char *original;
	char *modified;
} AssemblerLine;

typedef struct {
	AssemblerLine *lines[AssemblerLinesMax];
	size_t linesNext;
} AssemblerProgram;

int main(int argc, char **argv) {
	// Parse arguments
	if (argc!=3) {
		printf("Usage: %s inputfile outputfile\n", argv[0]);
		return 1;
	}

	const char *inputPath=argv[1];
	const char *outputPath=argv[2];

	// Parse input file
	FILE *inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		return 1;
	}

	// Read line-by-line
	AssemblerProgram *program=malloc(sizeof(AssemblerProgram)); // TODO: Check return
	program->linesNext=0;

	char *line=NULL;
	size_t lineSize=0;
	unsigned lineNum=1;
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

	// Temporary debugging
	printf("-------------------------------------------------------------------\n");
	for(unsigned i=0; i<program->linesNext; ++i) {
		AssemblerLine *assemblerLine=program->lines[i];

		if (strlen(assemblerLine->modified)>0)
			printf("%04u: '%s' -> '%s'\n", assemblerLine->lineNum, assemblerLine->original, assemblerLine->modified);
	}
	printf("-------------------------------------------------------------------\n");

	// Tidy up
	for(unsigned i=0; i<program->linesNext; ++i) {
		AssemblerLine *assemblerLine=program->lines[i];
		free(assemblerLine->original);
		free(assemblerLine->modified);
		free(assemblerLine);
	}

	return 0;
}
