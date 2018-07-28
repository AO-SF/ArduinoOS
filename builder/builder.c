#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/debug.h"
#include "../kernel/minifs.h"

#define totalSize (6*1024u)

uint8_t dataArray[totalSize];
MiniFs miniFs;

uint8_t readFunctor(uint16_t addr, void *userData);
void writeFunctor(uint16_t addr, uint8_t value, void *userData);

int main(int agrc, char **argv) {
	// format
	if (!miniFsFormat(&writeFunctor, NULL, totalSize)) {
		printf("could not format\n");
		return 1;
	}

	// mount
	if (!miniFsMountSafe(&miniFs, &readFunctor, &writeFunctor, NULL)) {
		printf("could not mount\n");
		return 1;
	}

	// Loop over files in ../binmockup and write each one to minifs
	debugMiniFsAddDir(&miniFs, "../binmockup");

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
