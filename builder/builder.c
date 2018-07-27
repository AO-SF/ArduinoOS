#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../kernel/minifs.h"

#define totalSize (2048u)

uint8_t dataArray[totalSize];
MiniFs miniFs;

uint8_t readFunctor(uint16_t addr, void *userData);
void writeFunctor(uint16_t addr, uint8_t value, void *userData);

int main(int agrc, char **argv) {
	// format
	if (!miniFsFormat(&writeFunctor, NULL, 2048)) {
		printf("could not format\n");
		return 1;
	}

	// mount
	if (!miniFsMountSafe(&miniFs, &readFunctor, &writeFunctor, NULL)) {
		printf("could not mount\n");
		return 1;
	}

	// Loop over files in ../binmockup and write each one to minifs
	DIR *dir=opendir("../binmockup");
	if (dir==NULL) {
		printf("could not open directory ../binmockup\n");
		return 1;
	}

	struct dirent *dp;
	while((dp=readdir(dir))!=NULL) {
		if (strcmp(dp->d_name, ".gitignore")==0)
			continue;

		char fullName[100];
		sprintf(fullName , "%s/%s", "../binmockup",dp->d_name);

		struct stat stbuf;
		if (stat(fullName, &stbuf)==-1) {
			printf("warning unable to stat file: %s\n", fullName);
			continue;
		}

		if ((stbuf.st_mode & S_IFMT)==S_IFDIR)
			continue; // Skip directories
		else {
			// Open src file for reading
			FILE *file=fopen(fullName, "r");
			if (file==NULL) {
				printf("warning unable to open file '%s' for reading\n", fullName);
				continue;
			}

			// Create file in minifs volume to represent this onernel.c:
			fseek(file, 0L, SEEK_END);
			uint16_t fileSize=ftell(file);
			if (!miniFsFileCreate(&miniFs, dp->d_name, fileSize)) {
				printf("warning unable to create file '%s' representing '%s'\n", dp->d_name, fullName);
				fclose(file);
				continue;
			}

			// Copy data from file to file
			fseek(file, 0L, SEEK_SET);
			for(uint16_t offset=0; 1; ++offset) {
				int value=fgetc(file);
				if (value==-1)
					break;
				if (!miniFsFileWrite(&miniFs, dp->d_name, offset, value)) {
					printf("warning unable to write complete data for '%s' representing '%s' (managed %i bytes)\n", dp->d_name, fullName, offset);
					break;
				}
			}

			fclose(file);
		}
	}

	// Debug fs
	miniFsDebug(&miniFs);

	// unmount to save any changes
	miniFsUnmount(&miniFs);

	// create binprogmem.c file
	FILE *file=fopen("../kernel/binprogmem.c", "w+");
	if (file==NULL) {
		printf("could not open ../kernel/binprogmem.c for writing\n");
		return 1;
	}

	fprintf(file, "// NOTE: File auto-generated (see builder)\n\n");
	fprintf(file, "#include \"binprogmem.h\"\n\n");
	fprintf(file, "const uint8_t binProgmemData[%u]={\n", totalSize);
	fprintf(file, "\t");
	const int perLine=16;
	for(int i=0; i<totalSize; ++i) {
		fprintf(file, "0x%02X", dataArray[i]);
		if (i+1<totalSize)
			fprintf(file, ",");
		if ((i+1)%perLine==0) {
			fprintf(file, " // ");
			for(int j=0; j<perLine; ++j) {
				int c=dataArray[i+1-perLine+j];
				fprintf(file, "%c", isgraph(c) ? c : '.');
			}
			fprintf(file, "\n");
			if (i+1!=totalSize)
				fprintf(file, "\t");
		}
	}
	fprintf(file, "};\n");

	fclose(file);

	return 0;
}

uint8_t readFunctor(uint16_t addr, void *userData) {
	assert(addr<totalSize);
	return dataArray[addr];
}

void writeFunctor(uint16_t addr, uint8_t value, void *userData) {
	assert(addr<totalSize);
	dataArray[addr]=value;
}
