#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/debug.h"
#include "../kernel/minifs.h"

uint8_t dataArray[8*1024]; // current minifs limit is 8kb anyway

bool buildVolume(const char *name, uint16_t size, const char *srcDir);

uint8_t readFunctor(uint16_t addr, void *userData);
void writeFunctor(uint16_t addr, uint8_t value, void *userData);

int main(int agrc, char **argv) {
	buildVolume("bin", 6*1024, "../binmockup");
	buildVolume("libstdio", 7*1024, "../binutilities/lib/std/io");
	buildVolume("libstdmath", 7*1024, "../binutilities/lib/std/math");
	buildVolume("libstdproc", 7*1024, "../binutilities/lib/std/proc");
	buildVolume("libstdstr", 7*1024, "../binutilities/lib/std/str");

	return 0;
}

bool buildVolume(const char *name, uint16_t size, const char *srcDir) {
	MiniFs miniFs;

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

	// Loop over files in ../binmockup and write each one to minifs
	debugMiniFsAddDir(&miniFs, srcDir);

	// Debug fs
	miniFsDebug(&miniFs);

	// unmount to save any changes
	miniFsUnmount(&miniFs);

	// create .c file
	char cFilePath[1024]; // TODO: better
	char hFilePath[1024]; // TODO: better
	sprintf(cFilePath, "../kernel/progmem%s.c", name);
	sprintf(hFilePath, "../kernel/progmem%s.h", name);

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
			fprintf(cFile, "\n");
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
