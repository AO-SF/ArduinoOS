#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"
#include "minifsextra.h"

bool builderCompact=false;

uint8_t dataArray[MINIFSMAXSIZE];

#define eepromTotalSize (4*1024)
#define eepromEtcOffset 0
#define eepromEtcSize (1*1024)
#define eepromHomeOffset eepromEtcSize
#define eepromHomeSize (eepromTotalSize-eepromHomeOffset)
const char *fakeEepromPath="./eeprom";

bool buildVolume(const char *name, const char *srcDir);
bool buildVolumeTrySize(const char *name, uint16_t size, const char *srcDir, bool verbose);

uint8_t readFunctor(uint16_t addr, void *userData);
void writeFunctor(uint16_t addr, uint8_t value, void *userData);

uint8_t homeMiniFsReadFunctor(uint16_t addr, void *userData);
void homeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);

uint8_t etcMiniFsReadFunctor(uint16_t addr, void *userData);
void etcMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);

int main(int argc, char **argv) {
	if (argc>2) {
		printf("usage: %s [--compact]\n", argv[0]);
		return 0;
	}

	if (argc>1) {
		if (strcmp(argv[1], "--compact")==0) {
			builderCompact=true;
		} else {
			printf("warning: unknown option '%s'\n", argv[1]);
		}
	}

	// Create /etc and mock /home in EEPROM file, if it does not look like it already exists
	FILE *fakeEepromFile=fopen(fakeEepromPath, "r+");
	if (fakeEepromFile!=NULL) {
		fclose(fakeEepromFile);
	} else {
		fakeEepromFile=fopen(fakeEepromPath, "a+");
		if (fakeEepromFile!=NULL) {
			for(unsigned i=0; i<eepromTotalSize; ++i)
				fputc(0xFF, fakeEepromFile);
			fclose(fakeEepromFile);

			fakeEepromFile=fopen(fakeEepromPath, "r+");
			if (fakeEepromFile!=NULL) {
				// Format /etc
				MiniFs etcMiniFs;
				miniFsFormat(&etcMiniFsWriteFunctor, fakeEepromFile, eepromEtcSize);
				miniFsMountSafe(&etcMiniFs, &etcMiniFsReadFunctor, &etcMiniFsWriteFunctor, fakeEepromFile);
				miniFsExtraAddDir(&etcMiniFs, "./src/tools/builder/mockups/etcmockup", true);
				miniFsDebug(&etcMiniFs);
				miniFsUnmount(&etcMiniFs);

				// Format /home and add a few example files
				MiniFs homeMiniFs;
				miniFsFormat(&homeMiniFsWriteFunctor, fakeEepromFile, eepromHomeSize);
				miniFsMountSafe(&homeMiniFs, &homeMiniFsReadFunctor, &homeMiniFsWriteFunctor, fakeEepromFile);
				miniFsExtraAddDir(&homeMiniFs, "./src/tools/builder/mockups/homemockup", true);
				miniFsDebug(&homeMiniFs);
				miniFsUnmount(&homeMiniFs);

				fclose(fakeEepromFile);
			}
		}
	}
	fakeEepromFile=NULL;

	// Build other read-only volumes
	buildVolume("bin", "./src/tools/builder/mockups/binmockup");
	buildVolume("usrbin", "./src/tools/builder/mockups/usrbinmockup");
	buildVolume("usrgames", "./src/tools/builder/mockups/usrgamesmockup");

	buildVolume("libstdio", "./src/userspace/bin/lib/std/io");
	buildVolume("libstdmath", "./src/userspace/bin/lib/std/math");
	buildVolume("libstdmem", "./src/userspace/bin/lib/std/mem");
	buildVolume("libstdproc", "./src/userspace/bin/lib/std/proc");
	buildVolume("libstdstr", "./src/userspace/bin/lib/std/str");
	buildVolume("libstdtime", "./src/userspace/bin/lib/std/time");
	buildVolume("libcurses", "./src/userspace/bin/lib/curses");
	buildVolume("libpin", "./src/userspace/bin/lib/pin");
	buildVolume("libsys", "./src/userspace/bin/lib/sys");

	buildVolume("man1", "./src/userspace/man/1");
	buildVolume("man2", "./src/userspace/man/2");
	buildVolume("man3", "./src/userspace/man/3");

	return 0;
}

bool buildVolume(const char *name, const char *srcDir) {
	// Loop, trying increasing powers of 2 for max volume size until we succeed (if we do at all).
	uint16_t size;
	for(size=MINIFSMINSIZE; size<=MINIFSMAXSIZE; size*=2) {
		if (buildVolumeTrySize(name, size, srcDir, false)) {
			// Not in compact mode? If so, done
			if (!builderCompact)
				return true;

			// Otherwise try to shrink
			// TODO: Use a binary search or similar, rather than linear
			uint16_t compactSize;
			for(compactSize=size-MINIFSFACTOR; compactSize>=MINIFSMINSIZE; compactSize-=MINIFSFACTOR) {
				if (!buildVolumeTrySize(name, compactSize, srcDir, false))
					break;
			}

			// Fall back on last size found
			if (buildVolumeTrySize(name, compactSize+MINIFSFACTOR, srcDir, false))
				return true;
		}
	}

	// We have failed - run final size again but with logging turned on.
	return buildVolumeTrySize(name, MINIFSMAXSIZE, srcDir, true);
}

bool buildVolumeTrySize(const char *name, uint16_t size, const char *srcDir, bool verbose) {
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
	sprintf(hFilePath, "src/kernel/progmem%s.h", name);

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

uint8_t homeMiniFsReadFunctor(uint16_t addr, void *userData) {
	assert(userData!=NULL);
	FILE *fakeEepromFile=(FILE *)userData;
	int res=fseek(fakeEepromFile, addr+eepromHomeOffset, SEEK_SET);
	assert(res==0);
	assert(ftell(fakeEepromFile)==addr+eepromHomeOffset);
	int c=fgetc(fakeEepromFile);
	assert(c!=EOF);
	return c;
}

void homeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	assert(userData!=NULL);
	FILE *fakeEepromFile=(FILE *)userData;
	int res=fseek(fakeEepromFile, addr+eepromHomeOffset, SEEK_SET);
	assert(res==0);
	assert(ftell(fakeEepromFile)==addr+eepromHomeOffset);
	res=fputc(value, fakeEepromFile);
	assert(res==value);
}

uint8_t etcMiniFsReadFunctor(uint16_t addr, void *userData) {
	assert(userData!=NULL);
	FILE *fakeEepromFile=(FILE *)userData;
	int res=fseek(fakeEepromFile, addr+eepromEtcOffset, SEEK_SET);
	assert(res==0);
	assert(ftell(fakeEepromFile)==addr+eepromEtcOffset);
	int c=fgetc(fakeEepromFile);
	assert(c!=EOF);
	return c;
}

void etcMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	assert(userData!=NULL);
	FILE *fakeEepromFile=(FILE *)userData;
	int res=fseek(fakeEepromFile, addr+eepromEtcOffset, SEEK_SET);
	assert(res==0);
	assert(ftell(fakeEepromFile)==addr+eepromEtcOffset);
	res=fputc(value, fakeEepromFile);
	assert(res==value);
}
