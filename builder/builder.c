#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
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
			// Create file in minifs volume to represent this one
			uint16_t fileSize=32; // TODO: Get this properly
			if (!miniFsFileCreate(&miniFs, dp->d_name,fileSize)) {
				printf("warning unable to create file '%s' representing '%s'\n", dp->d_name, fullName);
				continue;
			}

			// TODO: Write data once miniFs has functions implemented
		}
	}

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
	for(unsigned i=0; i<totalSize; ++i) {
		fprintf(file, "0x%02X", dataArray[i]);
		if (i+1<totalSize)
			fprintf(file, ",");
		if ((i+1)%16==0) {
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
