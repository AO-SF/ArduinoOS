#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kernelfs.h"

// TODO: Move to PROGMEM presumably.
typedef enum {
	KernelFsFixedFileRoot,
	KernelFsFixedFileDev,
	KernelFsFixedFileDevTtyUsb,
	KernelFsFixedFileDevZero,
	KernelFsFixedFileDevNull,
	KernelFsFixedFileDevRandom,
	KernelFsFixedFileDevURandom,
	KernelFsFixedFileNB,
} KernelFsFixedFile;

const char *kernelFsFixedFilePath[KernelFsFixedFileNB]={
	[KernelFsFixedFileRoot]="/",
	[KernelFsFixedFileDev]="/dev",
	[KernelFsFixedFileDevTtyUsb]="/dev/ttyUSB",
	[KernelFsFixedFileDevZero]="/dev/zero",
	[KernelFsFixedFileDevNull]="/dev/null",
	[KernelFsFixedFileDevRandom]="/dev/random",
	[KernelFsFixedFileDevURandom]="/dev/urandom",
};

typedef struct {
	char *fdt[KernelFsFdMax];
} KernelFsData;

KernelFsData kernelFsData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsFixed(const char *path); // does this path refer to a fixed file?
KernelFsFixedFile kernelFsPathGetFixed(const char *path); // Returns KernelFsFixedFileNB if path does not match a fixed file

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void) {
	// Clear file descriptor table
	for(int i=0; i<KernelFsFdMax; ++i)
		kernelFsData.fdt[i]=NULL;

	// Connect to serial and mount as /dev/ttyUSB.
#ifdef ARDUINO
	Serial.begin(9600);
	while (!Serial) ;
#endif

	// Mount two EEPROM mini file systems as /kernel and /home.
	// TODO: this
}

void kernelFsQuit(void) {
	// Free memory used in file descriptor table.
	for(int i=0; i<KernelFsFdMax; ++i) {
		free(kernelFsData.fdt[i]);
		kernelFsData.fdt[i]=NULL;
	}

	// Close serial connection.
#ifdef ARDUINO
	Serial.end();
#endif

	// Dismount two EEPROM mini file systems.
	// TODO: this
}

bool kernelFsFileExists(const char *path) {
	// Check for fixed path
	if (kernelFsPathIsFixed(path))
		return true;

	// TODO: Check for other files

	return false;
}

KernelFsFd kernelFsFileOpen(const char *path, KernelFsFileOpenFlags flags) {
	// Check if this file is already open and also look for an empty slot to use if not.
	KernelFsFd newFd=KernelFsFdInvalid;
	for(int i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (kernelFsData.fdt[i]==NULL)
			newFd=i; // If we suceed we can use this slot
		else if (strcmp(path, kernelFsData.fdt[i])==0)
			return KernelFsFdInvalid; // File is already open
	}

	// Check file exists.
	if (!kernelFsFileExists(path))
		return KernelFsFdInvalid;

	// Update file descriptor table.
	kernelFsData.fdt[newFd]=malloc(strlen(path)+1);
	if (kernelFsData.fdt[newFd]==NULL)
		return KernelFsFdInvalid; // Out of memory

	strcpy(kernelFsData.fdt[newFd], path);

	return newFd;
}

void kernelFsFileClose(KernelFsFd fd) {
	// Clear from file descriptor table.
	free(kernelFsData.fdt[fd]);
	kernelFsData.fdt[fd]=NULL;
}

KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kernelFsData.fdt[fd]==NULL)
		return 0;

	// Is this a fixed file?
	KernelFsFixedFile fixedFile=kernelFsPathGetFixed(kernelFsData.fdt[fd]);
	if (fixedFile!=KernelFsFixedFileNB) {
		// Decide how to act
		switch(fixedFile) {
			case KernelFsFixedFileRoot:
			case KernelFsFixedFileDev:
				return 0;
			break;
			case KernelFsFixedFileDevTtyUsb: {
#ifdef ARDUINO
				// TODO: this (use Serial.read)
#else
				ssize_t readRet=read(STDIN_FILENO, data, dataLen);
				if (readRet<0)
					return 0;
				return readRet;
#endif
			} break;
			case KernelFsFixedFileDevZero:
				memset(data, 0, dataLen);
				return dataLen;
			break;
			case KernelFsFixedFileDevNull:
				return 0;
			break;
			case KernelFsFixedFileDevRandom:
				// TODO: Genuine random data not pseudo
				for(KernelFsFileOffset i=0; i<dataLen; ++i)
					data[i]=(rand()&0xFF);
				return dataLen;
			break;
			case KernelFsFixedFileDevURandom:
				// TODO: Better algo based on same /dev/random pool (but still PRNG)
				for(KernelFsFileOffset i=0; i<dataLen; ++i)
					data[i]=(rand()&0xFF);
				return dataLen;
			break;
			case KernelFsFixedFileNB:
				assert(false);
				return 0;
			break;
		}

		assert(false);
		return 0;
	}

	// Handle standard files.
	// TODO: this

	return 0;
}

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kernelFsData.fdt[fd]==NULL)
		return 0;

	// Is this a fixed file?
	KernelFsFixedFile fixedFile=kernelFsPathGetFixed(kernelFsData.fdt[fd]);
	if (fixedFile!=KernelFsFixedFileNB) {
		// Decide how to act
		switch(fixedFile) {
			case KernelFsFixedFileRoot:
			case KernelFsFixedFileDev:
				return 0;
			break;
			case KernelFsFixedFileDevTtyUsb: {
#ifdef ARDUINO
				// TODO: this (use Serial.write)
#else
				ssize_t writeRet=write(STDIN_FILENO, data, dataLen);
				if (writeRet<0)
					return 0;
				return writeRet;
#endif
			} break;
			case KernelFsFixedFileDevZero:
				return 0;
			break;
			case KernelFsFixedFileDevNull:
				return dataLen; // consume all data
			break;
			case KernelFsFixedFileDevRandom:
			case KernelFsFixedFileDevURandom:
				return 0;
			break;
			case KernelFsFixedFileNB:
				assert(false);
				return 0;
			break;
		}

		assert(false);
		return 0;
	}

	// Handle standard files.
	// TODO: this

	return 0;
}

bool kernelFsPathIsValid(const char *path) {
	// All paths are absolute
	if (path[0]!='/')
		return false;

	return true;
}

void kernelFsPathNormalise(char *path) {
	if (!kernelFsPathIsValid(path))
		return;

	// TODO: this (e.g. replace '//' with '/')
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsFixed(const char *path) {
	return (kernelFsPathGetFixed(path)!=KernelFsFixedFileNB);
}

KernelFsFixedFile kernelFsPathGetFixed(const char *path) {
	// Search through fixed file paths
	for(int i=0; i<KernelFsFixedFileNB; ++i)
		if (strcmp(path, kernelFsFixedFilePath[i])==0)
			return i;

	return KernelFsFixedFileNB;
}
