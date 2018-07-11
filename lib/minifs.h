#ifndef MINIFS_H
#define MINIFS_H

#include <stdbool.h>
#include <stdint.h>

#define MINIFSMAXFILES 63

typedef uint8_t (MiniFsReadFunctor)(uint16_t addr);
typedef void (MiniFsWriteFunctor)(uint16_t addr, uint8_t value);

typedef struct {
	// Members are to be considered private
	MiniFsReadFunctor *readFunctor;
	MiniFsWriteFunctor *writeFunctor; // NULL if read-only

	uint8_t openBitset[(MINIFSMAXFILES+7)/8];
} MiniFs;

typedef uint8_t MiniFsFileDescriptor;
#define MiniFsFileDescriptorNone 0

////////////////////////////////////////////////////////////////////////////////
// Volume functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFormat(MiniFsWriteFunctor *writeFunctor, uint16_t totalSize); // Total size may be rounded down

// In the following two functions the readFunctor is required, but the writeFunctor may be null to mount as read-only.
bool miniFsMountFast(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor); // No integrity checking performed (at all)
bool miniFsMountSafe(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor); // Verify the header is sensible before mounting
void miniFsUnmount(MiniFs *fs);

bool miniFsGetReadOnly(const MiniFs *fs);
uint16_t miniFsGetTotalSize(const MiniFs *fs); // Total size available for whole file system (including metadata)

void miniFsDebug(const MiniFs *fs);

////////////////////////////////////////////////////////////////////////////////
// File functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFileExists(const MiniFs *fs, const char *filename);

// The following two functions return MiniFsFileDescriptorNone if the file could not be opened/created.
MiniFsFileDescriptor miniFsFileOpenRO(const MiniFs *fs, const char *filename);
MiniFsFileDescriptor miniFsFileOpenRW(MiniFs *fs, const char *filename, bool create);

void miniFsFileClose(MiniFs *fs, MiniFsFileDescriptor fd);

MiniFsFileDescriptor miniFsFileResize(MiniFs *fs, MiniFsFileDescriptor fd, uint16_t newLen); // Returns new file descriptor (which may or may not be the same as the one given).

/*
.....
uint16_t miniFsFileRead(const MiniFs *fs, MiniFsFileDescriptor fd, uint16_t offset, uint8_t *data, uint16_t dataLen); // Attempts to read up to dataLen number of items from the file at position offset. Returns the number of items successfully read.
uint16_t miniFsFileWrite(MiniFs *fs, MiniFsFileDescriptor fd, uint16_t offset, const uint8_t *data, uint16_t dataLen); // Will attempt to extend the file if required. Returns the number of items written.
*/

#endif
