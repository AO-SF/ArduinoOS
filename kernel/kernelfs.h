#ifndef KERNELFS_H
#define KERNELFS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t KernelFsFileOffset;

typedef uint8_t KernelFsFd; // file-descriptor
#define KernelFsFdInvalid 0
#define KernelFsFdMax 256

#define KernelPathMax 63

typedef int (KernelFsCharacterDeviceReadFunctor)(void); // returns -1 on failure
typedef bool (KernelFsCharacterDeviceWriteFunctor)(uint8_t value);

typedef bool (KernelFsDirectoryDeviceGetChildFunctor)(unsigned childNum, char childPath[KernelPathMax]);

typedef enum {
	KernelFsBlockDeviceFormatCustomMiniFs,
	KernelFsBlockDeviceFormatNB,
} KernelFsBlockDeviceFormat;

typedef int (KernelFsBlockDeviceReadFunctor)(KernelFsFileOffset addr); // returns -1 on failure
typedef bool (KernelFsBlockDeviceWriteFunctor)(KernelFsFileOffset addr, uint8_t value);

////////////////////////////////////////////////////////////////////////////////
// Initialisation etc
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void);
void kernelFsQuit(void);

////////////////////////////////////////////////////////////////////////////////
// Virtual device functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsAddCharacterDeviceFile(const char *mountPoint, KernelFsCharacterDeviceReadFunctor *readFunctor, KernelFsCharacterDeviceWriteFunctor *writeFunctor);
bool kernelFsAddDirectoryDeviceFile(const char *mountPoint, KernelFsDirectoryDeviceGetChildFunctor *getChildFunctor);
bool kernelFsAddBlockDeviceFile(const char *mountPoint, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, KernelFsBlockDeviceReadFunctor *readFunctor, KernelFsBlockDeviceWriteFunctor *writeFunctor);

////////////////////////////////////////////////////////////////////////////////
// File functions -including directories (all paths are expected to be valid and normalised)
////////////////////////////////////////////////////////////////////////////////

bool kernelFsFileExists(const char *path);

bool kernelFsFileCreate(const char *path);
bool kernelFsFileDelete(const char *path);

KernelFsFd kernelFsFileOpen(const char *path); // File/directory must exist. Returns KernelFsFdInvalid on failure to open.
void kernelFsFileClose(KernelFsFd fd); // Accepts KernelFsFdInvalid (doing nothing).

// The following functions are for non-directory files only.
KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes read
KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes written

// The following functions are for directory files only.
bool kernelFsDirectoryGetChild(KernelFsFd fd, unsigned childNum, char childPath[KernelPathMax]);

////////////////////////////////////////////////////////////////////////////////
// Path functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsValid(const char *path); // All paths are absolute so must start with '/'.

void kernelFsPathNormalise(char *path); // Simplifies a path in-place by substitutions such as '//'->'/'.

#endif
