#include <stdlib.h>
#include <stdio.h>

#include "minifs.h"

// Create an array of 1024 bytes and a couple of wrappers to fake the Arduino EEPROM.
#define STORAGESIZE (4*1024)
uint8_t storage[STORAGESIZE];

uint8_t readFunctor(uint16_t addr) {
	return storage[addr];
}

void writeFunctor(uint16_t addr, uint8_t value) {
	storage[addr]=value;
}

// Main code
int main(int argc, char **argv) {
	// Format
	if (!miniFsFormat(&writeFunctor, STORAGESIZE)) {
		printf("Could not format (size %u)\n", STORAGESIZE);
		return 0;
	}
	printf("Formatted new volume (size %u)\n", STORAGESIZE);

	// Mount
	MiniFs fs;
	if (!miniFsMountSafe(&fs, &readFunctor, &writeFunctor)) {
		printf("Could not mount volume\n");
		return 0;
	}

	printf("Mounted volume (size %u, read only %u)\n", miniFsGetTotalSize(&fs), miniFsGetReadOnly(&fs));

	// File tests
	const char *filenameA="lol.bmp";
	const char *filenameB="test.txt";

	printf("File '%s' exists: %i\n", filenameA, miniFsFileExists(&fs, filenameA));
	printf("File '%s' exists: %i\n", filenameB, miniFsFileExists(&fs, filenameB));

	MiniFsFileDescriptor fdA=miniFsFileOpenRW(&fs, filenameA, true);
	if (fdA==MiniFsFileDescriptorNone)
		printf("Could not create and open file '%s'\n", filenameA);
	else {
		printf("Created and opened file '%s'\n", filenameA);

		MiniFsFileDescriptor fdA2=miniFsFileOpenRW(&fs, filenameA, false);
		if (fdA2==MiniFsFileDescriptorNone)
			printf("Could not re-open file '%s' (as expected)\n", filenameA);
		else {
			printf("Re-opened file '%s' despite it being open already\n", filenameA);
			miniFsFileClose(&fs, fdA2);
		}
	}

	MiniFsFileDescriptor fdB=miniFsFileOpenRW(&fs, filenameB, true);
	if (fdB==MiniFsFileDescriptorNone)
		printf("Could not create and open file '%s'\n", filenameB);
	else
		printf("Created and opened file '%s'\n", filenameB);
	miniFsFileClose(&fs, fdB);

	printf("File '%s' exists: %i\n", filenameA, miniFsFileExists(&fs, filenameA));
	printf("File '%s' exists: %i\n", filenameB, miniFsFileExists(&fs, filenameB));

	const char *filenameLong="loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong";
	MiniFsFileDescriptor fdLong=miniFsFileOpenRW(&fs, filenameLong, true);
	miniFsFileClose(&fs, fdLong);

	for(int i=0; i<10; ++i) {
		char filenameLoop[32];
		sprintf(filenameLoop, "log%03i", i);
		MiniFsFileDescriptor fdLoop=miniFsFileOpenRW(&fs, filenameLoop, true);
		miniFsFileClose(&fs, fdLoop);
	}

	miniFsDebug(&fs);

	miniFsFileClose(&fs, fdA);

	// Unmount
	miniFsUnmount(&fs);
	printf("Unmounted\n");

	return 0;
}
