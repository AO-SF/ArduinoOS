#include <stdlib.h>
#include <stdio.h>

#include "minifs.h"

// Create an array of 1024 bytes and a couple of wrappers to fake the Arduino EEPROM.
#define STORAGESIZE 1024
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

	MiniFsFile file;

	if (!miniFsFileOpenRW(&file, &fs, filenameA, true)) {
		printf("Could not create and open file '%s'\n", filenameA);
	} else {
		printf("Created file '%s'\n", filenameA);

		miniFsFileClose(&file, &fs);
	}

	if (!miniFsFileOpenRW(&file, &fs, filenameB, true)) {
		printf("Could not create and open file '%s'\n", filenameB);
	} else {
		printf("Created file '%s'\n", filenameB);

		miniFsFileClose(&file, &fs);
	}

	printf("File '%s' exists: %i\n", filenameA, miniFsFileExists(&fs, filenameA));
	printf("File '%s' exists: %i\n", filenameB, miniFsFileExists(&fs, filenameB));

	miniFsDebug(&fs);

	// Unmount
	miniFsUnmount(&fs);
	printf("Unmounted\n");

	return 0;
}
