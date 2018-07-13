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
} MiniFs;

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

#endif
