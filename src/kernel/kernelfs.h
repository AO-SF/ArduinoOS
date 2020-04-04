#ifndef KERNELFS_H
#define KERNELFS_H

#include <stdbool.h>
#include <stdint.h>

#include "kstr.h"

// Define file offset/length type.
// User space can only use 16 bit addresses and lengths, but kernel can use 32 bit.
// For example, a volume mounted with the mount syscall could access all 4gb of an
// sd card at /dev/sd if mounted at /media/sd.
// From user space reading /dev/sd would be limited to first 64kb, but using mount
// all files in /media/sd could be read up to 64kb.
typedef uint32_t KernelFsFileOffset;
#define KernelFsFileOffsetMax INT32_MAX

typedef uint8_t KernelFsFd; // file-descriptor
#define KernelFsFdInvalid 0
#define KernelFsFdMax 64

#define KernelFsPathMax 64

typedef uint8_t KernelFsBlockDeviceFormat;
#define KernelFsBlockDeviceFormatCustomMiniFs 0
#define KernelFsBlockDeviceFormatFlatFile 1
#define KernelFsBlockDeviceFormatNB 2
#define KernelFsBlockDeviceFormatBits 1

// The enum and single 'generic' functor below are used as a way to provide and store multiple functors while only needing enough RAM for a single function pointer.
// Note that not all arguments are used in each case, and the return value is not always a uint32_t - see individual prototypes in enum commments for nore details.
typedef enum {
	// Common functors
	KernelFsDeviceFunctorTypeCommonFlush, // typedef bool (KernelFsDeviceFlushFunctor)(KernelFsDeviceFunctorTypeCommonFlush, void *userData);
	// Character device functors
	KernelFsDeviceFunctorTypeCharacterRead, // typedef int16_t (KernelFsCharacterDeviceReadFunctor)(KernelFsDeviceFunctorTypeCharacterRead, void *userData); - read and return a single character, or -1 on failure
	KernelFsDeviceFunctorTypeCharacterCanRead, // typedef bool (KernelFsCharacterDeviceCanReadFunctor)(KernelFsDeviceFunctorTypeCharacterCanRead, void *userData); - returns true if there is at least 1 byte available to read immediately
	KernelFsDeviceFunctorTypeCharacterWrite, // typedef KernelFsFileOffset (KernelFsCharacterDeviceWriteFunctor)(KernelFsDeviceFunctorTypeCharacterWrite, void *userData, const uint8_t *data, KernelFsFileOffset len); - returns number of bytes written
	// Block device functors
	KernelFsDeviceFunctorTypeBlockRead, // typedef KernelFsFileOffset (KernelFsBlockDeviceReadFunctor)(KernelFsDeviceFunctorTypeBlockRead, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr); - returns -1 on failure
	KernelFsDeviceFunctorTypeBlockWrite, // typedef KernelFsFileOffset (KernelFsBlockDeviceWriteFunctor)(KernelFsDeviceFunctorTypeBlockWrite, void *userData, const uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
} KernelFsDeviceFunctorType;

typedef uint32_t (KernelFsDeviceFunctor)(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);

////////////////////////////////////////////////////////////////////////////////
// Initialisation etc
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void);
void kernelFsQuit(void);

////////////////////////////////////////////////////////////////////////////////
// Virtual device functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsAddCharacterDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, bool canOpenMany, bool writable);
bool kernelFsAddDirectoryDeviceFile(KStr mountPoint);
bool kernelFsAddBlockDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, bool writable);

////////////////////////////////////////////////////////////////////////////////
// File functions -including directories (all paths are expected to be valid and normalised)
////////////////////////////////////////////////////////////////////////////////

bool kernelFsFileExists(const char *path);
bool kernelFsFileIsOpen(const char *path);
bool kernelFsFileIsOpenByFd(KernelFsFd fd);
bool kernelFsFileIsDir(const char *path);
bool kernelFsFileIsDirEmpty(const char *path);
bool kernelFsFileIsCharacter(const char *path);
KernelFsFileOffset kernelFsFileGetLen(const char *path);

bool kernelFsFileCreate(const char *path);
bool kernelFsFileCreateWithSize(const char *path, KernelFsFileOffset size);
bool kernelFsFileDelete(const char *path); // fails if file is open, or path is a non-empty directory (can remove devices files also, unmounting as required)

bool kernelFsFileFlush(const char *path);

bool kernelFsFileResize(const char *path, KernelFsFileOffset newSize); // path must not be open

KernelFsFd kernelFsFileOpen(const char *path); // File/directory must exist. Returns KernelFsFdInvalid on failure to open.
void kernelFsFileClose(KernelFsFd fd); // Accepts KernelFsFdInvalid (doing nothing).

KStr kernelFsGetFilePath(KernelFsFd fd);

// The following functions are for non-directory files only.
KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes read
KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen); // offset is ignored for character device files. Returns number of bytes read. blocking only affects some character device files
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
