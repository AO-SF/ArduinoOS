#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"
#include "minifsextra.h"

bool builderCompact=false;

uint8_t dataArray[MINIFSMAXSIZE];

bool buildVolume(const char *name, const char *srcDir, const char *destDir);
bool buildVolumeTrySize(const char *name, uint16_t size, const char *srcDir, const char *destDir, bool verbose);

uint8_t readFunctor(uint16_t addr, void *userData);
void writeFunctor(uint16_t addr, uint8_t value, void *userData);

int main(int argc, char **argv) {
	// Parse arguments
	if (argc<4) {
		printf("usage: %s [--size=SIZE] srcdir volumename destdir\n", argv[0]);
		return 0;
	}

	uint16_t maxSize=0; // 0 means use minimum required
	for(int i=1; i<argc-3; ++i) {
		if (strncmp(argv[i], "--size=", strlen("--size="))==0) {
			maxSize=atoi(argv[i]+strlen("--size="));
		} else {
			printf("warning: unknown option '%s'\n", argv[i]);
		}
	}
	const char *srcDir=argv[argc-3];
	const char *volumeName=argv[argc-2];
	const char *destDir=argv[argc-1];

	// Build volue
	if (maxSize==0)
		buildVolume(volumeName, srcDir, destDir);
	else
		buildVolumeTrySize(volumeName, maxSize, srcDir, destDir, true);

	return 0;
}

bool buildVolume(const char *name, const char *srcDir, const char *destDir) {
	// Loop, trying increasing powers of 2 for max volume size until we succeed (if we do at all).
	uint16_t size;
	for(size=MINIFSMINSIZE; size<=MINIFSMAXSIZE; size*=2) {
		if (buildVolumeTrySize(name, size, srcDir, destDir, false)) {
			// Not in compact mode? If so, done
			if (!builderCompact)
				return true;

			// Otherwise try to shrink with a binary search
			uint16_t minGoodSize=size;
			uint16_t maxBadSize=(size>MINIFSMINSIZE ? size/2 : MINIFSMINSIZE);
			while(minGoodSize-MINIFSFACTOR>maxBadSize) {
				uint16_t trialSize=(maxBadSize+minGoodSize)/2;
				if (buildVolumeTrySize(name, trialSize, srcDir, destDir, false))
					minGoodSize=trialSize;
				else
					maxBadSize=trialSize;
			}

			// Use minimum size found
			if (buildVolumeTrySize(name, minGoodSize, srcDir, destDir, false))
				return true;
		}
	}

	// We have failed - run final size again but with logging turned on.
	return buildVolumeTrySize(name, MINIFSMAXSIZE, srcDir, destDir, true);
}

bool buildVolumeTrySize(const char *name, uint16_t size, const char *srcDir, const char *destDir, bool verbose) {
	MiniFs miniFs;

	// clear data arary (not strictly necessary but might avoid confusion in the future when e.g. stdio functions are in unused part of the stdmath volume)
	// setting to 0xFF also matches value stored in uninitialised Arduino EEPROM
	memset(dataArray, 0xFF, sizeof(dataArray));

	// format
	if (!miniFsFormat(&writeFunctor, NULL, size)) {
		if (verbose)
			printf("could not format\n");
		return false;
	}

	// mount
	if (!miniFsMountSafe(&miniFs, &readFunctor, &writeFunctor, NULL)) {
		if (verbose)
			printf("could not mount\n");
		return false;
	}

	// Loop over files in given source dir and write each one to minifs
	if (!miniFsExtraAddDir(&miniFs, srcDir, verbose)) {
		if (verbose)
			printf("could not add dir '%s'\n", srcDir);
		return false;
	}

	// Debug fs
	//miniFsDebug(&miniFs);

	// unmount to save any changes
	miniFsUnmount(&miniFs);

	// create .h file
	char hFilePath[1024]; // TODO: better
	sprintf(hFilePath, "%s/progmem%s.h", destDir, name);

	FILE *hFile=fopen(hFilePath, "w+");
	if (hFile==NULL) {
		if (verbose)
			printf("could not open '%s' for writing\n", hFilePath);
		return false;
	}

	fprintf(hFile, "// NOTE: File auto-generated (see builder)\n\n");
	fprintf(hFile, "#ifndef PROGMEM%s_H\n", name);
	fprintf(hFile, "#define PROGMEM%s_H\n\n", name);
	fprintf(hFile, "#include <stdint.h>\n\n");

	fprintf(hFile, "#define PROGMEM%sDATASIZE %iu\n", name, size);
	fprintf(hFile, "#ifdef ARDUINO\n");
	fprintf(hFile, "#include <avr/pgmspace.h>\n");
	fprintf(hFile, "static const uint8_t progmem%sData[PROGMEM%sDATASIZE] PROGMEM ={\n", name, name);
	fprintf(hFile, "#else\n");
	fprintf(hFile, "static const uint8_t progmem%sData[PROGMEM%sDATASIZE]={\n", name, name);
	fprintf(hFile, "#endif\n");
	fprintf(hFile, "\t");
	const int perLine=16;
	for(int i=0; i<size; ++i) {
		fprintf(hFile, "0x%02X", dataArray[i]);
		if (i+1<size)
			fprintf(hFile, ",");
		if ((i+1)%perLine==0) {
			fprintf(hFile, " // ");
			for(int j=0; j<perLine; ++j) {
				int c=dataArray[i+1-perLine+j];
				fprintf(hFile, "%c", isgraph(c) ? c : '.');
			}
			fprintf(hFile, " END\n");
			if (i+1!=size)
				fprintf(hFile, "\t");
		}
	}
	fprintf(hFile, "};\n");

	fprintf(hFile, "#endif\n");

	fclose(hFile);

	return true;
}

uint8_t readFunctor(uint16_t addr, void *userData) {
	return dataArray[addr];
}

void writeFunctor(uint16_t addr, uint8_t value, void *userData) {
	dataArray[addr]=value;
}
