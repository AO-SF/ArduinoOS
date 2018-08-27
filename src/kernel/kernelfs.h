#ifndef KERNELFS_H
#define KERNELFS_H

#include <stdbool.h>
#include <stdint.h>

#include "kstr.h"

typedef uint16_t KernelFsFileOffset;

typedef uint8_t KernelFsFd; // file-descriptor
#define KernelFsFdInvalid 0
#define KernelFsFdMax 64

#define KernelFsPathMax 32

typedef int16_t (KernelFsCharacterDeviceReadFunctor)(void *userData); // returns -1 on failure
typedef bool (KernelFsCharacterDeviceCanReadFunctor)(void *userData);
typedef bool (KernelFsCharacterDeviceWriteFunctor)(uint8_t value, void *userData);

typedef uint8_t KernelFsBlockDeviceFormat;
#define KernelFsBlockDeviceFormatCustomMiniFs 0
#define KernelFsBlockDeviceFormatFlatFile 1
#define KernelFsBlockDeviceFormatNB 2

typedef int16_t (KernelFsBlockDeviceReadFunctor)(KernelFsFileOffset addr, void *userData); // returns -1 on failure
typedef bool (KernelFsBlockDeviceWriteFunctor)(KernelFsFileOffset addr, uint8_t value, void *userData);

////////////////////////////////////////////////////////////////////////////////
// Initialisation etc
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void);
void kernelFsQuit(void);

////////////////////////////////////////////////////////////////////////////////
// Virtual device functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsAddCharacterDeviceFile(KStr mountPoint, KernelFsCharacterDeviceReadFunctor *readFunctor, KernelFsCharacterDeviceCanReadFunctor *canReadFunctor, KernelFsCharacterDeviceWriteFunctor *writeFunctor, bool canOpenMany, void *functorUserData);
bool kernelFsAddDirectoryDeviceFile(KStr mountPoint);
bool kernelFsAddBlockDeviceFile(KStr mountPoint, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, KernelFsBlockDeviceReadFunctor *readFunctor, KernelFsBlockDeviceWriteFunctor *writeFunctor, void *functorUserData);

////////////////////////////////////////////////////////////////////////////////
// File functions -including directories (all paths are expected to be valid and normalised)
////////////////////////////////////////////////////////////////////////////////

bool kernelFsFileExists(const char *path);
bool kernelFsFileIsOpen(const char *path);
bool kernelFsFileIsDir(const char *path);
bool kernelFsFileIsDirEmpty(const char *path);
KernelFsFileOffset kernelFsFileGetLen(const char *path);

bool kernelFsFileCreate(const char *path);
bool kernelFsFileCreateWithSize(const char *path, KernelFsFileOffset size);
bool kernelFsFileDelete(const char *path); // fails if file is open, or path is a non-empty directory (can remove devices files also, unmounting as required)

bool kernelFsFileResize(const char *path, KernelFsFileOffset newSize); // path must not be open

KernelFsFd kernelFsFileOpen(const char *path); // File/directory must exist. Returns KernelFsFdInvalid on failure to open.
void kernelFsFileClose(KernelFsFd fd); // Accepts KernelFsFdInvalid (doing nothing).

KStr kernelFsGetFilePath(KernelFsFd fd);

// The following functions are for non-directory files only.
KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes read
KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen, bool block); // offset is ignored for character device files. Returns number of bytes read. blocking only affects some character device files
bool kernelFsFileCanRead(KernelFsFd fd); // character device files may return false if a read would block, all other files return true (as they never block)
KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes written
KernelFsFileOffset kernelFsFileWriteOffset(KernelFsFd fd, KernelFsFileOffset offset, const uint8_t *data, KernelFsFileOffset dataLen); // offset is ignored for character device files. Returns number of bytes written

// The following functions are for directory files only.
bool kernelFsDirectoryGetChild(KernelFsFd fd, unsigned childNum, char childPath[KernelFsPathMax]);

////////////////////////////////////////////////////////////////////////////////
// Path functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsValid(const char *path); // All paths are absolute so must start with '/'.

void kernelFsPathNormalise(char *path); // Simplifies a path in-place by substitutions such as '//'->'/'.

void kernelFsPathSplit(char *path, char **dirnamePtr, char **basenamePtr); // Modifies given path, which must be kept around as long as dirname and basename are needed
void kernelFsPathSplitStatic(const char *path, char **dirnamePtr, char **basenamePtr); // like kernelFsPathSplit but makes a copy of the given path (to a global buffer, and thus not re-entrant)
void kernelFsPathSplitStaticKStr(KStr kstr, char **dirnamePtr, char **basenamePtr);

#endif
