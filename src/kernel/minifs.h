#ifndef MINIFS_H
#define MINIFS_H

#include <stdbool.h>
#include <stdint.h>

#include "kstr.h"

#define MINIFSFACTOR 128u // 1<=factor<=256, increasing allows for a greater total volume size, but wastes more space padding small files (so their length is a multiple of the factor)
#define MINIFSMINSIZE 128u // lcm(factor, 2headersize)=lcm(128,2*64)=128
#define MINIFSMAXSIZE (MINIFSFACTOR*256) // we use an 8 bit value with a factor to represent the total size (with factor=128 this allows up to 32kb)

#define MINIFSMAXFILES (MINIFSFACTOR-2)

#define MiniFsPathMax (MINIFSFACTOR-1)

typedef uint16_t (MiniFsReadFunctor)(uint16_t addr, uint8_t *data, uint16_t len, void *userData);
typedef uint16_t (MiniFsWriteFunctor)(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

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
bool miniFsIsEmpty(const MiniFs *fs); // equivalent to: miniFsGetChildCount()==0, but usually much faster

void miniFsDebug(const MiniFs *fs);

////////////////////////////////////////////////////////////////////////////////
// File functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFileExists(const MiniFs *fs, const char *filename);
bool miniFsFileExistsKStr(const MiniFs *fs, KStr filename);
uint16_t miniFsFileGetLen(const MiniFs *fs, const char *filename);
uint16_t miniFsFileGetSize(const MiniFs *fs, const char *filename);

bool miniFsFileCreate(MiniFs *fs, const char *filename, uint16_t size);
bool miniFsFileDelete(MiniFs *fs, const char *filename);

bool miniFsFileResize(MiniFs *fs, const char *filename, uint16_t newSize);

uint16_t miniFsFileRead(const MiniFs *fs, const char *filename, uint16_t offset, uint8_t *data, uint16_t len); // Returns number of bytes read
uint16_t miniFsFileReadKStr(const MiniFs *fs, KStr filename, uint16_t offset, uint8_t *data, uint16_t len); // Returns number of bytes read
uint16_t miniFsFileWrite(MiniFs *fs, const char *filename, uint16_t offset, const uint8_t *data, uint16_t len);

#endif
