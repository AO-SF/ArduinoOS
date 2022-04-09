#ifndef FAT_H
#define FAT_H

#include <stdbool.h>
#include <stdint.h>

#define FATPATHMAX 64 // for compatability with rest of OS even though should be ~255
#define FATMAXFILES 128

typedef uint32_t (FatReadFunctor)(uint32_t addr, uint8_t *data, uint32_t len, void *userData);
typedef uint32_t (FatWriteFunctor)(uint32_t addr, const uint8_t *data, uint32_t len, void *userData);

typedef struct {
	// Members are to be considered private

	FatReadFunctor *readFunctor;
	FatWriteFunctor *writeFunctor;
	void *userData;

	uint16_t bytesPerSector;
	uint16_t fatSector;
	uint16_t rootDirSector;
	uint8_t type;
} Fat;

////////////////////////////////////////////////////////////////////////////////
// Volume functions
////////////////////////////////////////////////////////////////////////////////

// In the following two functions the readFunctor is required, but the writeFunctor may be null to mount as read-only.
bool fatMountFast(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData); // No integrity checking performed (at all)
bool fatMountSafe(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData); // Verify the volume appears sensible before mounting
void fatUnmount(Fat *fs);

void fatDebug(const Fat *fs);

bool fatGetChildN(const Fat *fs, unsigned childNum, char childPath[FATPATHMAX]); // n<FATMAXFILES, no gaps

////////////////////////////////////////////////////////////////////////////////
// File functions
////////////////////////////////////////////////////////////////////////////////

#endif