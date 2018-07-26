#ifndef MINIFS_H
#define MINIFS_H

#include <stdbool.h>
#include <stdint.h>

#define MINIFSMAXFILES 63

#define MiniFsPathMax 63

typedef uint8_t (MiniFsReadFunctor)(uint16_t addr, void *userData);
typedef void (MiniFsWriteFunctor)(uint16_t addr, uint8_t value, void *userData);

typedef struct {
	// Members are to be considered private
	MiniFsReadFunctor *readFunctor;
	MiniFsWriteFunctor *writeFunctor; // NULL if read-only
	void *functorUserData;
} MiniFs;

////////////////////////////////////////////////////////////////////////////////
// Volume functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFormat(MiniFsWriteFunctor *writeFunctor, void *functorUserData, uint16_t totalSize); // Total size may be rounded down

// In the following two functions the readFunctor is required, but the writeFunctor may be null to mount as read-only.
bool miniFsMountFast(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor, void *functorUserData); // No integrity checking performed (at all)
bool miniFsMountSafe(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor, void *functorUserData); // Verify the header is sensible before mounting
void miniFsUnmount(MiniFs *fs);

bool miniFsGetReadOnly(const MiniFs *fs);
uint16_t miniFsGetTotalSize(const MiniFs *fs); // Total size available for whole file system (including metadata)

bool miniFsGetChildN(const MiniFs *fs, unsigned childNum, char childPath[MiniFsPathMax]); // n<MINIFSMAXFILES, gaps
uint8_t miniFsGetChildCount(const MiniFs *fs);

void miniFsDebug(const MiniFs *fs);

////////////////////////////////////////////////////////////////////////////////
// File functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFileExists(const MiniFs *fs, const char *filename);
uint16_t miniFsFileGetLen(const MiniFs *fs, const char *filename);
uint16_t miniFsFileGetSize(const MiniFs *fs, const char *filename);

bool miniFsFileCreate(MiniFs *fs, const char *filename, uint16_t size);
bool miniFsFileDelete(MiniFs *fs, const char *filename);

int miniFsFileRead(const MiniFs *fs, const char *filename, uint16_t offset); // Returns -1 on failure
bool miniFsFileWrite(MiniFs *fs, const char *filename, uint16_t offset, uint8_t value);

#endif
