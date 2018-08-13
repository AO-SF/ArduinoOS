#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifsextra.h"

uint8_t dataArray[8*1024]; // current minifs limit is 8kb anyway

#define eepromSize (8*1024)
const char *fakeEepromPath="./eeprom";

bool buildVolume(const char *name, uint16_t size, const char *srcDir);

uint8_t readFunctor(uint16_t addr, void *userData);
void writeFunctor(uint16_t addr, uint8_t value, void *userData);

uint8_t homeMiniFsReadFunctor(uint16_t addr, void *userData);
void homeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);

int main(int agrc, char **argv) {
	// Create mock /home EEPROM file if it does not look like it already exists
	FILE *fakeEepromFile=fopen(fakeEepromPath, "r+");
	if (fakeEepromFile!=NULL) {
		fclose(fakeEepromFile);
	} else {
		fakeEepromFile=fopen(fakeEepromPath, "a+");
		if (fakeEepromFile!=NULL) {
			for(unsigned i=0; i<eepromSize; ++i)
				fputc(0xFF, fakeEepromFile);
			fclose(fakeEepromFile);

			fakeEepromFile=fopen(fakeEepromPath, "r+");
			if (fakeEepromFile!=NULL) {
				MiniFs homeMiniFs;
				if (miniFsMountSafe(&homeMiniFs, &homeMiniFsReadFunctor, &homeMiniFsWriteFunctor, fakeEepromFile)) {
					miniFsUnmount(&homeMiniFs);
				} else {
					miniFsFormat(&homeMiniFsWriteFunctor, fakeEepromFile, eepromSize);

					// Add a few example files
					miniFsMountSafe(&homeMiniFs, &homeMiniFsReadFunctor, &homeMiniFsWriteFunctor, fakeEepromFile);
					miniFsExtraAddDir(&homeMiniFs, "./src/tools/builder/mockups/homemockup");
					miniFsDebug(&homeMiniFs);
					miniFsUnmount(&homeMiniFs);
				}

				fclose(fakeEepromFile);
			}
		}
	}
	fakeEepromFile=NULL;

	// Build other read-only volumes
	buildVolume("bin", 8*1024, "./src/tools/builder/mockups/binmockup");
	buildVolume("usrbin", 8*1024, "./src/tools/builder/mockups/usrbinmockup");
	buildVolume("libstdio", 8*1024, "./src/userspace/bin/lib/std/io");
	buildVolume("libstdmath", 8*1024, "./src/userspace/bin/lib/std/math");
	buildVolume("libstdmem", 8*1024, "./src/userspace/bin/lib/std/mem");
	buildVolume("libstdproc", 8*1024, "./src/userspace/bin/lib/std/proc");
	buildVolume("libstdstr", 8*1024, "./src/userspace/bin/lib/std/str");
	buildVolume("libstdtime", 8*1024, "./src/userspace/bin/lib/std/time");
	buildVolume("libcurses", 8*1024, "./src/userspace/bin/lib/curses");
	buildVolume("libpin", 8*1024, "./src/userspace/bin/lib/pin");

	buildVolume("man1", 8*1024, "./src/userspace/man/1");
	buildVolume("man2", 8*1024, "./src/userspace/man/2");
	buildVolume("man3", 8*1024, "./src/userspace/man/3");

	return 0;
}

bool buildVolume(const char *name, uint16_t size, const char *srcDir) {
	MiniFs miniFs;

	// clear data arary (not strictly necessary but might avoid confusion in the future when e.g. stdio functions are in unused part of the stdmath volume)
	// setting to 0xFF also matches value stored in uninitialised Arduino EEPROM
	memset(dataArray, 0xFF, sizeof(dataArray));

	// format
	if (!miniFsFormat(&writeFunctor, NULL, size)) {
		printf("could not format\n");
		return false;
	}

	// mount
	if (!miniFsMountSafe(&miniFs, &readFunctor, &writeFunctor, NULL)) {
		printf("could not mount\n");
		return false;
	}

	// Loop over files in given source dir and write each one to minifs
	miniFsExtraAddDir(&miniFs, srcDir);

	// Debug fs
	//miniFsDebug(&miniFs);

	// unmount to save any changes
	miniFsUnmount(&miniFs);

	// create .c file
	char cFilePath[1024]; // TODO: better
	char hFilePath[1024]; // TODO: better
	sprintf(cFilePath, "src/kernel/progmem%s.c", name);
	sprintf(hFilePath, "src/kernel/progmem%s.h", name);

	FILE *cFile=fopen(cFilePath, "w+");
	if (cFile==NULL) {
		printf("could not open '%s' for writing\n", cFilePath);
		return false;
	}

	fprintf(cFile, "// NOTE: File auto-generated (see builder)\n\n");
	fprintf(cFile, "#include \"progmem%s.h\"\n\n", name);
	fprintf(cFile, "const uint8_t progmem%sData[%u]={\n", name, size);
	fprintf(cFile, "\t");
	const int perLine=16;
	for(int i=0; i<size; ++i) {
		fprintf(cFile, "0x%02X", dataArray[i]);
		if (i+1<size)
			fprintf(cFile, ",");
		if ((i+1)%perLine==0) {
			fprintf(cFile, " // ");
			for(int j=0; j<perLine; ++j) {
				int c=dataArray[i+1-perLine+j];
				fprintf(cFile, "%c", isgraph(c) ? c : '.');
			}
			fprintf(cFile, " END\n");
			if (i+1!=size)
				fprintf(cFile, "\t");
		}
	}
	fprintf(cFile, "};\n");

	fclose(cFile);

	// create .h file
	FILE *hFile=fopen(hFilePath, "w+");
	if (hFile==NULL) {
		printf("could not open '%s' for writing\n", hFilePath);
		return false;
	}

	fprintf(hFile, "// NOTE: File auto-generated (see builder)\n\n");
	fprintf(hFile, "#ifndef PROGMEM%s_H\n", name);
	fprintf(hFile, "#define PROGMEM%s_H\n\n", name);
	fprintf(hFile, "#include <stdint.h>\n\n");
	fprintf(hFile, "// TODO: this needs to be done specially for arduino\n");
	fprintf(hFile, "#define PROGMEM%sDATASIZE %iu\n", name, size);
	fprintf(hFile, "extern const uint8_t progmem%sData[PROGMEM%sDATASIZE];\n\n", name, name);
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
	int res=fseek(fakeEepromFile, addr, SEEK_SET);
	assert(res==0);
	assert(ftell(fakeEepromFile)==addr);
	int c=fgetc(fakeEepromFile);
	assert(c!=EOF);
	return c;
}

void homeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	assert(userData!=NULL);
	FILE *fakeEepromFile=(FILE *)userData;
	int res=fseek(fakeEepromFile, addr, SEEK_SET);
	assert(res==0);
	assert(ftell(fakeEepromFile)==addr);
	res=fputc(value, fakeEepromFile);
	assert(res==value);
}
