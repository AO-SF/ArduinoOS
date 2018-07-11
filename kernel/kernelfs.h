#ifndef KERNELFS_H
#define KERNELFS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t KernelFsFileOffset;

typedef uint8_t KernelFsFd; // file-descriptor
#define KernelFsFdInvalid 0
#define KernelFsFdMax 256

#define KernelPathMax 63

typedef enum {
	KernelFsFileOpenFlagsNone=0,
	KernelFsFileOpenFlagsRO=1,
	KernelFsFileOpenFlagsRW=2|KernelFsFileOpenFlagsRO,
	KernelFsFileOpenFlagsCreate=4,
	KernelFsFileOpenFlagsRWC=KernelFsFileOpenFlagsRW|KernelFsFileOpenFlagsCreate,
} KernelFsFileOpenFlags;

////////////////////////////////////////////////////////////////////////////////
// Initialisation etc
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void);
void kernelFsQuit(void);

////////////////////////////////////////////////////////////////////////////////
// File functions -including directories (all paths are expected to be valid and normalised)
////////////////////////////////////////////////////////////////////////////////

bool kernelFsFileExists(const char *path);

KernelFsFd kernelFsFileOpen(const char *path, KernelFsFileOpenFlags flags); // Returns KernelFsFdInvalid on failure to open/create.
void kernelFsFileClose(KernelFsFd fd); // Accepts KernelFsFdInvalid (doing nothing).

// The following functions are for non-directory files only.
KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes read
KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes written

////////////////////////////////////////////////////////////////////////////////
// Path functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsValid(const char *path); // All paths are absolute so must start with '/'.

void kernelFsPathNormalise(char *path); // Simplifies a path in-place by substitutions such as '//'->'/'.

#endif
