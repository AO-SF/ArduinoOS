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

typedef uint8_t KernelFsFdMode;
#define KernelFsFdModeNone 0
#define KernelFsFdModeRO 1
#define KernelFsFdModeWO 2
#define KernelFsFdModeRW 3
#define KernelFsFdModeBits 2
#define KernelFsFdModeMax ((1u)<<KernelFsFdModeBits)

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
	KernelFsDeviceFunctorTypeCharacterCanWrite, // typedef bool (KernelFsCharacterDeviceCanWriteFunctor)(KernelFsDeviceFunctorTypeCharacterCanWrite, void *userData); - returns true if there is at least space to write 1 byte immediately (or at least it is not know to be full)
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

void kernelFsRemoveDeviceFile(const char *mountPoint); // note: unlike kernelFsFileDelete this does not require virtual directory to be empty

void *kernelFsDeviceFileGetUserData(const char *mountPoint);

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

// Open allocates a new fd with ref count set to 1
// Dupe increments ref count of given fd, unless it has hit the limit
// DupeOrOpen tries to act as Dupe, and if successful returns the given fd. Otherwise then tries to act as Open, passing on its return value. This is a thin wrapper around the other two calls.
KernelFsFd kernelFsFileOpen(const char *path, KernelFsFdMode mode); // File/directory must exist. Returns KernelFsFdInvalid on failure to open.
bool kernelFsFileDupe(KernelFsFd fd); // Increases the ref count for the given fd (on failure leaves ref count unchanged and returns false)
KernelFsFd kernelFsFileDupeOrOpen(KernelFsFd fd); // See above
unsigned kernelFsFileClose(KernelFsFd fd); // Reduces ref count for the given fd, and if it goes to 0 then closes the fd. Accepts KernelFsFdInvalid (doing nothing). Returns new ref count

KStr kernelFsGetFilePath(KernelFsFd fd); // returns a null kstr if fd not open
unsigned kernelFsGetFileRefCount(KernelFsFd fd); // returns 0 if fd not open
KernelFsFdMode kernelFsGetFileMode(KernelFsFd fd); // returns KernelFsFdModeNone if fd not open

// The following functions are for non-directory files only.
KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes read
KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen); // offset is ignored for character device files. Returns number of bytes read.
bool kernelFsFileCanRead(KernelFsFd fd); // character device files may return false if a read would block, all other files return true (as they never block)

bool kernelFsFileReadByte(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *value);
bool kernelFsFileReadWord(KernelFsFd fd, KernelFsFileOffset offset, uint16_t *value);
bool kernelFsFileReadDoubleWord(KernelFsFd fd, KernelFsFileOffset offset, uint32_t *value);

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen); // Returns number of bytes written
KernelFsFileOffset kernelFsFileWriteOffset(KernelFsFd fd, KernelFsFileOffset offset, const uint8_t *data, KernelFsFileOffset dataLen); // offset is ignored for character device files. Returns number of bytes written
bool kernelFsFileCanWrite(KernelFsFd fd); // character device files may return false if a write would block, all other files return true (as they never block)

bool kernelFsFileWriteByte(KernelFsFd fd, KernelFsFileOffset offset, uint8_t value);
bool kernelFsFileWriteWord(KernelFsFd fd, KernelFsFileOffset offset, uint16_t value);
bool kernelFsFileWriteDoubleWord(KernelFsFd fd, KernelFsFileOffset offset, uint32_t value);

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

////////////////////////////////////////////////////////////////////////////////
// Misc functions
////////////////////////////////////////////////////////////////////////////////

const char *kernelFsFdModeToString(KernelFsFdMode mode);

// These two functions can be passed to the miniFsMount functions to allow reading/writing a MiniFs volume in an open file,
// with the fd passed as the userData field (cast via uintptr_t)
uint16_t kernelFsFdMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t kernelFsFdMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

#endif
