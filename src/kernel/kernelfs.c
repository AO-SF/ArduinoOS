#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#else
#include <unistd.h>
#endif

#include "kernelfs.h"
#include "log.h"
#include "minifs.h"
#include "ktime.h"
#include "util.h"

#define KernelFsDevicesMax 128
typedef uint8_t KernelFsDeviceIndex;

typedef uint8_t KernelFsDeviceType;
#define KernelFsDeviceTypeBlock 0
#define KernelFsDeviceTypeCharacter 1
#define KernelFsDeviceTypeDirectory 2
#define KernelFsDeviceTypeNB 3
#define KernelFsDeviceTypeBits 2

STATICASSERT(KernelFsDeviceTypeBits+1+1+4==8);
typedef struct {
	KStr mountPoint;

	KernelFsDeviceFunctor *functor;
	void *userData;

	uint8_t type:KernelFsDeviceTypeBits; // type is KernelFsDeviceType
	uint8_t characterCanOpenManyFlag:1;
	uint8_t writable:1;
	uint8_t reserved:4;

	// Type-specific data follows
} KernelFsDeviceCommon;

typedef struct {
	KernelFsDeviceCommon common;
} KernelFsDeviceCharacter;

typedef struct {
	KernelFsDeviceCommon common;

	KernelFsFileOffset size;
	KernelFsBlockDeviceFormat format;
} KernelFsDeviceBlock;

typedef union {
	KernelFsDeviceCommon common; // can always be accessed
	KernelFsDeviceBlock block;
	KernelFsDeviceCharacter character;
} KernelFsDevice;

typedef struct {
	KStr path; // also stores ref counter in spare bits - see kstrGetSpare and kstrSetSpare
	KernelFsDeviceIndex deviceIndex;
} KernelFsFdtEntry;

typedef struct {
	KernelFsFdtEntry fdt[KernelFsFdMax];

	KernelFsDevice devices[KernelFsDevicesMax];
} KernelFsData;

KernelFsData kernelFsData;

char kernelFsPathSplitStaticBuf[KernelFsPathMax];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path);
bool kernelFsFileCanOpenMany(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPath(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPathKStr(KStr path);
KernelFsDeviceIndex kernelFsGetDeviceIndexFromDevice(const KernelFsDevice *device);

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsDeviceType type, bool writable);
void kernelFsRemoveDeviceFileRaw(KernelFsDevice *device);

bool kernelFsDeviceIsChildOfPath(KernelFsDevice *device, const char *parentDir);

bool kernelFsDeviceIsDir(const KernelFsDevice *device);
bool kernelFsDeviceIsDirEmpty(const KernelFsDevice *device);

uint16_t kernelFsMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t kernelFsMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

bool kernelFsDeviceInvokeFunctorCommonFlush(KernelFsDevice *device);

int16_t kernelFsDeviceInvokeFunctorCharacterRead(KernelFsDevice *device);
bool kernelFsDeviceInvokeFunctorCharacterCanRead(KernelFsDevice *device);
KernelFsFileOffset kernelFsDeviceInvokeFunctorCharacterWrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len);
bool kernelFsDeviceInvokeFunctorCharacterCanWrite(KernelFsDevice *device);

KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockRead(KernelFsDevice *device, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockwrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);

// The following functions deal with logic handling the mode and ref count stored in the spare bits of fdt path fields.
STATICASSERT(KernelFsFdModeNone==0); // so that make function can return 0 for error unambiguously
STATICASSERT(KStrSpareBits>=KernelFsFdModeBits+2); // make sure we have at least 2 bits for ref counts

#define KernelFsFdRefCountBits (KStrSpareBits-KernelFsFdModeBits)
#define KernelFsFdRefCountMax ((1u)<<KernelFsFdRefCountBits)

uint8_t kernelFsFdPathSpareMake(KernelFsFdMode mode, unsigned refCount); // returns 0 on error (bad mode or ref count)
KernelFsFdMode kernelFsFdPathSpareGetMode(uint8_t spare);
unsigned kernelFsFdPathSpareGetRefCount(uint8_t spare);

uint8_t kernelFsGetFileSpare(KernelFsFd fd);
void kernelFsSetFileSpare(KernelFsFd fd, uint8_t spare);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void) {
	// Clear file descriptor table
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i) {
		kernelFsData.fdt[i].path=kstrNull();
		kernelFsData.fdt[i].deviceIndex=KernelFsDevicesMax;
	}

	// Clear virtual device array
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i)
		kernelFsData.devices[i].common.type=KernelFsDeviceTypeNB;
}

void kernelFsQuit(void) {
	// Free memory used in file descriptor table.
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i) {
		kstrFree(&kernelFsData.fdt[i].path);
		kernelFsData.fdt[i].deviceIndex=KernelFsDevicesMax;
	}

	// Free virtual device array
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->common.type==KernelFsDeviceTypeNB)
			continue;
		kstrFree(&device->common.mountPoint);
		device->common.type=KernelFsDeviceTypeNB;
	}
}

bool kernelFsAddCharacterDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, bool canOpenMany, bool writable) {
	assert(!kstrIsNull(mountPoint));
	assert(functor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, functor, userData, KernelFsDeviceTypeCharacter, writable);
	if (device==NULL)
		return false;

	// Set specific fields.
	device->common.characterCanOpenManyFlag=(canOpenMany || !writable);

	return true;
}

bool kernelFsAddDirectoryDeviceFile(KStr mountPoint) {
	assert(!kstrIsNull(mountPoint));

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, NULL, NULL, KernelFsDeviceTypeDirectory, true);
	if (device==NULL)
		return false;

	return true;
}

bool kernelFsAddBlockDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, bool writable) {
	assert(!kstrIsNull(mountPoint));
	assert(functor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, functor, userData, KernelFsDeviceTypeBlock, writable);
	if (device==NULL)
		return false;
	device->block.format=format;
	device->block.size=size;

	// Attempt to mount
	switch(format) {
		case KernelFsBlockDeviceFormatCustomMiniFs: {
			MiniFs miniFs;
			if (!miniFsMountSafe(&miniFs, &kernelFsMiniFsReadWrapper, (writable ? &kernelFsMiniFsWriteWrapper : NULL), device))
				goto error;
			miniFsUnmount(&miniFs);
		} break;
		case KernelFsBlockDeviceFormatFlatFile:
		break;
		case KernelFsBlockDeviceFormatNB:
			goto error;
		break;
	}

	return true;

	error:
	kernelFsRemoveDeviceFileRaw(device);
	return false;
}

void kernelFsRemoveDeviceFile(const char *mountPoint) {
	assert(mountPoint!=NULL);

	KernelFsDevice *device=kernelFsGetDeviceFromPath(mountPoint);
	if (device!=NULL)
		kernelFsRemoveDeviceFileRaw(device);
}

void *kernelFsDeviceFileGetUserData(const char *mountPoint) {
	assert(mountPoint!=NULL);

	KernelFsDevice *device=kernelFsGetDeviceFromPath(mountPoint);
	if (device!=NULL)
		return device->common.userData;

	return NULL;
}

bool kernelFsFileExists(const char *path) {
	assert(path!=NULL);

	// Check for virtual device path
	if (kernelFsPathIsDevice(path))
		return true;

	// Find dirname and basename
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	// Check for node at dirname
	KernelFsDevice *device=kernelFsGetDeviceFromPath(dirname);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (device->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), device);
						bool res=miniFsFileExists(&miniFs, basename);
						miniFsUnmount(&miniFs);
						return res;
					}
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// Not used as directories
			break;
			case KernelFsDeviceTypeDirectory:
				// We have already checked for an explicit device above, so no more children
				return false;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	// No suitable node found
	return false;
}

bool kernelFsFileIsOpen(const char *path) {
	assert(path!=NULL);

	for(KernelFsFd i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (!kstrIsNull(kernelFsData.fdt[i].path) && kstrStrcmp(path, kernelFsData.fdt[i].path)==0)
			return true;
	}
	return false;
}

bool kernelFsFileIsOpenByFd(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	return !kstrIsNull(kernelFsData.fdt[fd].path);
}

bool kernelFsFileIsDir(const char *path) {
	assert(path!=NULL);

	// Currently directories can only exist as device files
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL)
		return false;

	// So check if this device is a directory.
	return kernelFsDeviceIsDir(device);
}

bool kernelFsFileIsDirEmpty(const char *path) {
	assert(path!=NULL);

	// Currently directories can only exist as device files
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL)
		return false;

	// So check if this device is a directory, and if it is empty.
	return kernelFsDeviceIsDirEmpty(device);
}

bool kernelFsFileIsCharacter(const char *path) {
	assert(path!=NULL);

	// Path must at least be a device file
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL)
		return false;

	// So check if this device is of the character type.
	return (device->common.type==KernelFsDeviceTypeCharacter);
}

KernelFsFileOffset kernelFsFileGetLen(const char *path) {
	assert(path!=NULL);

	// Invalid path?
	if (!kernelFsPathIsValid(path))
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
						return 0;
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return device->block.size;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				return 0;
			break;
			case KernelFsDeviceTypeDirectory:
				// This operation cannot be performed on a directory
				return 0;
			break;
			case KernelFsDeviceTypeNB:
			break;
		}
	}

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (parentDevice->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						KernelFsFileOffset res=miniFsFileGetLen(&miniFs, basename);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return 0;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// These are files and thus cannot contain other files
				return 0;
			break;
			case KernelFsDeviceTypeDirectory:
				// Device directories can only contain other virtual devices (which are handled above)
				return 0;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	return 0;
}

bool kernelFsFileCreate(const char *path) {
	assert(path!=NULL);

	return kernelFsFileCreateWithSize(path, 0);
}

bool kernelFsFileCreateWithSize(const char *path, KernelFsFileOffset size) {
	assert(path!=NULL);

	// Find dirname and basename
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	// Check for node at dirname
	KernelFsDevice *device=kernelFsGetDeviceFromPath(dirname);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						// In theory we can create files on a MiniFs if it is not mounted read only
						bool res=false;
						if (device->common.writable) {
							MiniFs miniFs;
							miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, &kernelFsMiniFsWriteWrapper, device);
							res=miniFsFileCreate(&miniFs, basename, size);
							miniFsUnmount(&miniFs);
						}
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// Not used as directories
			break;
			case KernelFsDeviceTypeDirectory:
				// Device directories are currently read-only
				return false;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	// No suitable node found
	return false;
}

bool kernelFsFileDelete(const char *path) {
	assert(path!=NULL);

	// Ensure this file is not open
	if (kernelFsFileIsOpen(path))
		return false;

	// If this is a directory, check if empty
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device!=NULL && kernelFsDeviceIsDir(device) && !kernelFsDeviceIsDirEmpty(device))
		return false;

	// Is this a virtual device file?
	if (device!=NULL) {
		// Type-specific logic
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// Nothing to do - we don't keep the volume mounted as mounting is free
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						// Nothing special to do
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
			case KernelFsDeviceTypeDirectory:
				// Nothing to do
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
				return false;
			break;
		}

		// Remove device
		kernelFsRemoveDeviceFileRaw(device);

		return true;
	}

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (parentDevice->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsFileDelete(&miniFs, basename);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return false;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// These cannot act as directories
				return false;
			break;
			case KernelFsDeviceTypeDirectory:
				// Currently these can only contain other virtual devices, and so we would have found it above
				return false;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	return false;
}

bool kernelFsFileFlush(const char *path) {
	assert(path!=NULL);

	// Invalid path?
	if (!kernelFsPathIsValid(path))
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device!=NULL)
		return kernelFsDeviceInvokeFunctorCommonFlush(device);

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL)
		return kernelFsDeviceInvokeFunctorCommonFlush(parentDevice);

	return false;
}

bool kernelFsFileResize(const char *path, KernelFsFileOffset newSize) {
	assert(path!=NULL);

	// Invalid path?
	if (!kernelFsPathIsValid(path))
		return false;

	// Ensure this file is not open
	if (kernelFsFileIsOpen(path))
		return false;

	// Is this a virtual device file?
	if (kernelFsGetDeviceFromPath(path)!=NULL) {
		// Cannot resize virtual devices
		return false;
	}

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						if (newSize>=UINT16_MAX)
							return false; // minifs limits files to 64kb
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (parentDevice->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsFileResize(&miniFs, basename, newSize);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return false;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
			case KernelFsDeviceTypeDirectory:
				return false;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	return false;
}

KernelFsFd kernelFsFileOpen(const char *path, KernelFsFdMode mode) {
	assert(path!=NULL);

	// Invalid path?
	if (!kernelFsPathIsValid(path))
		return KernelFsFdInvalid;

	// Invalid mode?
	if (mode==KernelFsFdModeNone || mode>KernelFsFdModeMax)
		return KernelFsFdInvalid;

	// Check file exists.
	if (!kernelFsFileExists(path))
		return KernelFsFdInvalid;

	// Check if this file is already open and also look for an empty slot to use if not.
	KernelFsFd newFd=KernelFsFdInvalid;
	bool alreadyOpen=false;
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (kstrIsNull(kernelFsData.fdt[i].path))
			newFd=i; // If we suceed we can use this slot
		else if (kstrStrcmp(path, kernelFsData.fdt[i].path)==0)
			alreadyOpen=true;
	}

	// If file is already open, decide if it can be openned more than once
	if (alreadyOpen && !kernelFsFileCanOpenMany(path))
		return KernelFsFdInvalid;

	// Update file descriptor table.
	kernelFsData.fdt[newFd].path=kstrC(path);
	if (kstrIsNull(kernelFsData.fdt[newFd].path))
		return KernelFsFdInvalid; // Out of memory

	kernelFsSetFileSpare(newFd, kernelFsFdPathSpareMake(mode, 1)); // store mode and refcount=1 in spare bits of path string

	// Grab device index to save doing this repeatedly in the future
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL) {
		// Must be child of a device file
		char *dirname, *basename;
		kernelFsPathSplitStatic(path, &dirname, &basename);

		device=kernelFsGetDeviceFromPath(dirname);

		// Still no device?
		if (device==NULL) {
			// File shouldn't have passed earlier kernelFsFileExists test but this code is here for safety.
			kernelFsData.fdt[newFd].path=kstrNull();
			return KernelFsFdInvalid;
		}
	}

	kernelFsData.fdt[newFd].deviceIndex=kernelFsGetDeviceIndexFromDevice(device);

	return newFd;
}

bool kernelFsFileDupe(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// Is the given fd even in use?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return false;

	// Increase ref count
	uint8_t spare=kernelFsGetFileSpare(fd);
	unsigned refCount=kernelFsFdPathSpareGetRefCount(spare);
	++refCount;
	if (refCount>=KernelFsFdRefCountMax)
		return false;
	kernelFsSetFileSpare(fd, kernelFsFdPathSpareMake(kernelFsFdPathSpareGetMode(spare), refCount));

	return true;
}

KernelFsFd kernelFsFileDupeOrOpen(KernelFsFd fd) {
	// First attempt to simply increase the ref count on the existing fd as this is very cheap
	if (kernelFsFileDupe(fd))
		return fd;

	// Otherwise try to open again with a new fd and a fresh ref count of 1
	KStr pathK=kernelFsGetFilePath(fd);
	if (kstrIsNull(pathK))
		return KernelFsFdInvalid;

	char path[KernelFsPathMax];
	kstrStrcpy(path, pathK);

	return kernelFsFileOpen(path, kernelFsGetFileMode(fd));
}

unsigned kernelFsFileClose(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// Is the given fd even in use?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Reduce ref count
	uint8_t spare=kernelFsGetFileSpare(fd);
	unsigned refCount=kernelFsFdPathSpareGetRefCount(spare);
	--refCount;
	kernelFsSetFileSpare(fd, kernelFsFdPathSpareMake(kernelFsFdPathSpareGetMode(spare), refCount));

	// If ref count is now 0, clear from file descriptor table.
	if (refCount==0) {
		kstrFree(&kernelFsData.fdt[fd].path);
		kernelFsData.fdt[fd].deviceIndex=KernelFsDevicesMax;
	}

	return refCount;
}

KStr kernelFsGetFilePath(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	return kernelFsData.fdt[fd].path;
}

unsigned kernelFsGetFileRefCount(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// Fd not open?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Extract ref count
	uint8_t spare=kernelFsGetFileSpare(fd);
	return kernelFsFdPathSpareGetRefCount(spare);
}

KernelFsFdMode kernelFsGetFileMode(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// Fd not open?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return KernelFsFdModeNone;

	// Extract mode
	uint8_t spare=kernelFsGetFileSpare(fd);
	return kernelFsFdPathSpareGetMode(spare);
}

KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen) {
	assert(fd<KernelFsFdMax);
	assert(data!=NULL);

	return kernelFsFileReadOffset(fd, 0, data, dataLen);
}

KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen) {
	assert(fd<KernelFsFdMax);
	assert(data!=NULL);

	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Bad mode?
	if (!(kernelFsGetFileMode(fd) & KernelFsFdModeRO))
		return 0;

	// Is this a virtual device file, or is it the child of one?
	KernelFsDevice *device=&kernelFsData.devices[kernelFsData.fdt[fd].deviceIndex];
	if (kstrDoubleStrcmp(kernelFsData.fdt[fd].path, device->common.mountPoint)==0) {
		assert(device==kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		// This fd IS the device cached in the fdt (rather than a child of it)
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return kernelFsDeviceInvokeFunctorBlockRead(device, data, dataLen, offset);
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter: {
				// offset is ignored as these are not seekable
				KernelFsFileOffset read;
				for(read=0; read<dataLen; ++read) {
					if (!kernelFsDeviceInvokeFunctorCharacterCanRead(device))
						break;
					int16_t c=kernelFsDeviceInvokeFunctorCharacterRead(device);
					if (c<0 || c>=256)
						break;
					data[read]=c;
				}
				return read;
			} break;
			case KernelFsDeviceTypeDirectory:
				// This operation cannot be performed on a directory
				return 0;
			break;
			case KernelFsDeviceTypeNB:
			break;
		}
	} else {
		assert(device!=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		// This fd is a child of the device cached in the fdt
		char *dirname, *basename;
		kernelFsPathSplitStaticKStr(kernelFsGetFilePath(fd), &dirname, &basename);

		assert(device==kernelFsGetDeviceFromPath(dirname));

		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						if (offset>=UINT16_MAX)
							return 0;
						if (dataLen>=UINT16_MAX)
							dataLen=UINT16_MAX;
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (device->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), device);
						uint16_t read=miniFsFileRead(&miniFs, basename, offset, data, dataLen);
						miniFsUnmount(&miniFs);
						return read;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return 0;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// These are files and thus cannot contain other files
				return 0;
			break;
			case KernelFsDeviceTypeDirectory:
				// Device directories can only contain other virtual devices (which are handled above)
				return 0;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	return 0;
}

bool kernelFsFileCanRead(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return false;

	// Bad mode?
	if (!(kernelFsGetFileMode(fd) & KernelFsFdModeRO))
		return false;

	// Is this a virtual character device file?
	KernelFsDevice *device=&kernelFsData.devices[kernelFsData.fdt[fd].deviceIndex];
	if (kstrDoubleStrcmp(kernelFsData.fdt[fd].path, device->common.mountPoint)==0) {
		assert(device==kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		if (device->common.type==KernelFsDeviceTypeCharacter)
			return kernelFsDeviceInvokeFunctorCharacterCanRead(device);
	} else {
		assert(device!=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));
	}

	// Otherwise all other file types never block
	return true;
}

bool kernelFsFileReadByte(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *value) {
	assert(fd<KernelFsFdMax);
	assert(value!=NULL);

	return (kernelFsFileReadOffset(fd, offset, value, 1)==1);
}

bool kernelFsFileReadWord(KernelFsFd fd, KernelFsFileOffset offset, uint16_t *value) {
	assert(fd<KernelFsFdMax);
	assert(value!=NULL);

	uint8_t data[2];
	if (kernelFsFileReadOffset(fd, offset, data, 2)!=2)
		return false;
	*value=(((uint16_t)data[0])<<8)|data[1];
	return true;
}

bool kernelFsFileReadDoubleWord(KernelFsFd fd, KernelFsFileOffset offset, uint32_t *value) {
	assert(fd<KernelFsFdMax);
	assert(value!=NULL);

	uint8_t data[4];
	if (kernelFsFileReadOffset(fd, offset, data, 4)!=4)
		return false;
	*value=(((uint32_t)data[0])<<24)|(((uint32_t)data[1])<<16)|(((uint32_t)data[2])<<8)|data[3];
	return true;
}

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen) {
	assert(fd<KernelFsFdMax);
	assert(data!=NULL);

	return kernelFsFileWriteOffset(fd, 0, data, dataLen);
}

KernelFsFileOffset kernelFsFileWriteOffset(KernelFsFd fd, KernelFsFileOffset offset, const uint8_t *data, KernelFsFileOffset dataLen) {
	assert(fd<KernelFsFdMax);
	assert(data!=NULL);

	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Bad mode?
	if (!(kernelFsGetFileMode(fd) & KernelFsFdModeWO))
		return 0;

	// Is this a virtual device file, or is it the child of one?
	KernelFsDevice *device=&kernelFsData.devices[kernelFsData.fdt[fd].deviceIndex];
	if (kstrDoubleStrcmp(kernelFsData.fdt[fd].path, device->common.mountPoint)==0) {
		// This fd IS the device cached in the fdt (rather than a child of it)
		assert(device==kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		if (!device->common.writable)
			return 0;

		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return kernelFsDeviceInvokeFunctorBlockwrite(device, data, dataLen, offset);
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter: {
				// offset is ignored as these are not seekable
				if (!kernelFsDeviceInvokeFunctorCharacterCanWrite(device))
					return 0;
				return kernelFsDeviceInvokeFunctorCharacterWrite(device, data, dataLen);
			} break;
			case KernelFsDeviceTypeDirectory:
				// This operation cannot be performed on a directory
				return 0;
			break;
			case KernelFsDeviceTypeNB:
			break;
		}
	} else {
		assert(device!=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		// This fd is a child of the devices cached in the fdt
		char *dirname, *basename;
		kernelFsPathSplitStaticKStr(kernelFsGetFilePath(fd), &dirname, &basename);

		assert(device==kernelFsGetDeviceFromPath(dirname));

		if (!device->common.writable)
			return 0;

		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						if (offset>=UINT16_MAX)
							return false;
						if (dataLen>=UINT16_MAX)
							dataLen=UINT16_MAX;
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, &kernelFsMiniFsWriteWrapper, device);
						KernelFsFileOffset res=miniFsFileWrite(&miniFs, basename, offset, data, dataLen);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return 0;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// These are files and thus cannot contain other files
				return 0;
			break;
			case KernelFsDeviceTypeDirectory:
				// Device directories can only contain other virtual devices (which are handled above)
				return 0;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	return 0;
}

bool kernelFsFileCanWrite(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// TODO: if we find a device, should we check the writable flag first?

	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return false;

	// Bad mode?
	if (!(kernelFsGetFileMode(fd) & KernelFsFdModeWO))
		return false;

	// Is this a virtual character device file?
	KernelFsDevice *device=&kernelFsData.devices[kernelFsData.fdt[fd].deviceIndex];
	if (kstrDoubleStrcmp(kernelFsData.fdt[fd].path, device->common.mountPoint)==0) {
		assert(device==kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		if (device->common.type==KernelFsDeviceTypeCharacter)
			return kernelFsDeviceInvokeFunctorCharacterCanWrite(device);
	} else {
		assert(device!=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));
	}

	// Otherwise all other file types never block
	return true;
}

bool kernelFsFileWriteByte(KernelFsFd fd, KernelFsFileOffset offset, uint8_t value) {
	assert(fd<KernelFsFdMax);

	return (kernelFsFileWriteOffset(fd, offset, &value, 1)==1);
}

bool kernelFsFileWriteWord(KernelFsFd fd, KernelFsFileOffset offset, uint16_t value) {
	assert(fd<KernelFsFdMax);

	uint8_t data[2]={value>>8, value&255};
	return (kernelFsFileWriteOffset(fd, offset, data, 2)==2);
}

bool kernelFsFileWriteDoubleWord(KernelFsFd fd, KernelFsFileOffset offset, uint32_t value) {
	assert(fd<KernelFsFdMax);

	uint8_t data[4]={value>>24, (value>>16)&255, (value>>8)&255, value&255};
	return (kernelFsFileWriteOffset(fd, offset, data, 4)==4);
}

bool kernelFsDirectoryGetChild(KernelFsFd fd, unsigned childNum, char childPath[KernelFsPathMax]) {
	assert(fd<KernelFsFdMax);

	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return false;

	// Bad mode?
	if (!(kernelFsGetFileMode(fd) & KernelFsFdModeRO))
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=&kernelFsData.devices[kernelFsData.fdt[fd].deviceIndex];
	if (kstrDoubleStrcmp(kernelFsData.fdt[fd].path, device->common.mountPoint)==0) {
		assert(device==kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFd j=0;
						for(KernelFsFd i=0; i<MINIFSMAXFILES; ++i) {
							kstrStrcpy(childPath, kernelFsData.fdt[fd].path);
							strcat(childPath, "/");
							MiniFs miniFs;
							miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (device->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), device);
							bool res=miniFsGetChildN(&miniFs, i, childPath+strlen(childPath));
							miniFsUnmount(&miniFs);
							if (!res)
								continue;
							if (j==childNum)
								return true;
							++j;
						}
						return false;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
				return false;
			break;
			case KernelFsDeviceTypeCharacter: {
				// Character files cannot be directories
				return false;
			} break;
			case KernelFsDeviceTypeDirectory: {
				// Check for virtual devices as children
				uint8_t foundCount=0;
				for(uint8_t i=0; i<KernelFsDevicesMax; ++i) {
					KernelFsDevice *childDevice=&kernelFsData.devices[i];
					if (childDevice->common.type==KernelFsDeviceTypeNB)
						continue;

					kstrStrcpy(childPath, kernelFsData.fdt[fd].path); // Borrow childPath as a generic buffer temporarily
					if (kernelFsDeviceIsChildOfPath(childDevice, childPath)) {
						if (foundCount==childNum) {
							kstrStrcpy(childPath, childDevice->common.mountPoint);
							return true;
						}
						++foundCount;
					}
				}
			} break;
			case KernelFsDeviceTypeNB:
			break;
		}
	}

	return false;
}

bool kernelFsPathIsValid(const char *path) {
	assert(path!=NULL);

	// All paths are absolute
	if (path[0]!='/')
		return false;

	// Only '/' root directory can end in a slash
	if (strcmp(path, "/")!=0 && path[strlen(path)-1]=='/')
		return false;

	return true;
}

void kernelFsPathNormalise(char *path) {
	assert(path!=NULL);

	// Add trailing slash to make . and .. logic simpler
	// TODO: Fix this hack (we may access one beyond what path allows)
	size_t preLen=strlen(path);
	path[preLen]='/';
	path[preLen+1]='\0';

	bool change;
	do {
		char *c;
		change=false;

		// Replace '/x/../' with '/', unless x is also ..
		c=path;
		while((c=strstr(c, "/../"))!=NULL) {
			// Look for last slash before this
			char *d;
			for(d=c-1; d>=path; --d) {
				if (*d=='/')
					break;
			}

			if (d>=path && strncmp(d, "/../", 4)!=0) {
				change=true;
				memmove(d, c+3, strlen(c+3)+1);
			}

			++c;
		}

		// Replace '/./' with '/'
		c=strstr(path, "/./");
		if (c!=NULL) {
			change=true;
			memmove(c, c+2, strlen(c+2)+1);
		}

		// Replace '//' with '/'
		c=strstr(path, "//");
		if (c!=NULL) {
			change=true;
			memmove(c, c+1, strlen(c+1)+1);
		}
	} while(change);

	// Trim trailing slash (except for root)
	if (strcmp(path, "/")!=0 && path[strlen(path)-1]=='/')
		path[strlen(path)-1]='\0';
}

void kernelFsPathSplit(char *path, char **dirnamePtr, char **basenamePtr) {
	assert(path!=NULL);
	assert(kernelFsPathIsValid(path));
	assert(dirnamePtr!=NULL);
	assert(basenamePtr!=NULL);

	// Work backwards looking for final slash
	char *lastSlash=strrchr(path, '/');
	assert(lastSlash!=NULL);
	*lastSlash='\0';
	*basenamePtr=lastSlash+1;
	*dirnamePtr=path;
}

void kernelFsPathSplitStatic(const char *path, char **dirnamePtr, char **basenamePtr) {
	assert(path!=NULL);
	assert(dirnamePtr!=NULL);
	assert(basenamePtr!=NULL);

	strcpy(kernelFsPathSplitStaticBuf, path);
	kernelFsPathSplit(kernelFsPathSplitStaticBuf, dirnamePtr, basenamePtr);
}

void kernelFsPathSplitStaticKStr(KStr kstr, char **dirnamePtr, char **basenamePtr) {
	assert(!kstrIsNull(kstr));
	assert(dirnamePtr!=NULL);
	assert(basenamePtr!=NULL);

	kstrStrcpy(kernelFsPathSplitStaticBuf, kstr);
	kernelFsPathSplit(kernelFsPathSplitStaticBuf, dirnamePtr, basenamePtr);
}

static const char *kernelFsFdModeToStringBadMode="??";
static const char *kernelFsFdModeToStringArray[]={
	[KernelFsFdModeNone]="NO",
	[KernelFsFdModeRO]="RO",
	[KernelFsFdModeWO]="WO",
	[KernelFsFdModeRW]="RW",
};
const char *kernelFsFdModeToString(KernelFsFdMode mode) {
	if (mode>=KernelFsFdModeMax)
		return kernelFsFdModeToStringBadMode;
	return kernelFsFdModeToStringArray[mode];
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path) {
	assert(path!=NULL);

	return (kernelFsGetDeviceFromPath(path)!=NULL);
}

bool kernelFsFileCanOpenMany(const char *path) {
	assert(path!=NULL);

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				return !device->common.writable;
			break;
			case KernelFsDeviceTypeCharacter:
				return device->common.characterCanOpenManyFlag;
			break;
			case KernelFsDeviceTypeDirectory:
				return true;
			break;
			case KernelFsDeviceTypeNB:
			break;
		}
	}

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return !parentDevice->common.writable;
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// These are files and thus cannot contain other files
			break;
			case KernelFsDeviceTypeDirectory:
				// Device directories can only contain other virtual devices (which are handled above)
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
			break;
		}
	}

	return false;
}

KernelFsDevice *kernelFsGetDeviceFromPath(const char *path) {
	assert(path!=NULL);

	KStr pathKStr=kstrAllocStatic((char *)path); // HACK: not const correct but kernelFsGetDeviceFromPathKStr doesn't modify its argument so this is safe
	return kernelFsGetDeviceFromPathKStr(pathKStr);
}

KernelFsDevice *kernelFsGetDeviceFromPathKStr(KStr path) {
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->common.type!=KernelFsDeviceTypeNB && kstrDoubleStrcmp(path, device->common.mountPoint)==0)
			return device;
	}
	return NULL;
}

KernelFsDeviceIndex kernelFsGetDeviceIndexFromDevice(const KernelFsDevice *device) {
	if (device==NULL)
		return KernelFsDevicesMax;
	return (((const uint8_t *)device)-((const uint8_t *)kernelFsData.devices))/sizeof(KernelFsDevice);
}

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsDeviceType type, bool writable) {
	assert(!kstrIsNull(mountPoint));
	assert(type<KernelFsDeviceTypeNB);

	// HACK: use kernelFsPathSplitStaticBuf here as we end up using it later anyway
	kstrStrcpy(kernelFsPathSplitStaticBuf, mountPoint);

	// Ensure this file does not already exist
	if (kernelFsFileExists(kernelFsPathSplitStaticBuf))
		return NULL;

	// Ensure the parent directory exists (skipped for root)
	if (kstrStrcmp("/", mountPoint)!=0) {
		char *dirname, *basename;
		kernelFsPathSplitStaticKStr(mountPoint, &dirname, &basename);

		if (strlen(dirname)==0) {
			// Special case for files in root directory
			if (!kernelFsFileExists("/") || !kernelFsFileIsDir("/"))
				return NULL;
		} else if (!kernelFsFileExists(dirname) || !kernelFsFileIsDir(dirname))
			return NULL;
	}

	// Look for an empty slot in the device table
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->common.type!=KernelFsDeviceTypeNB)
			continue;

		device->common.mountPoint=mountPoint;
		device->common.functor=functor;
		device->common.userData=userData;
		device->common.type=type;
		device->common.writable=writable;

		return device;
	}

	return NULL;
}

void kernelFsRemoveDeviceFileRaw(KernelFsDevice *device) {
	assert(device!=NULL);

	// Does this device file even exist?
	if (device->common.type==KernelFsDeviceTypeNB)
		return;

	// Clear type and free memory
	device->common.type=KernelFsDeviceTypeNB;
	kstrFree(&device->common.mountPoint);
	device->common.mountPoint=kstrNull();
}

bool kernelFsDeviceIsChildOfPath(KernelFsDevice *device, const char *parentDir) {
	assert(device!=NULL);
	assert(parentDir!=NULL);

	// Invalid path?
	if (!kernelFsPathIsValid(parentDir))
		return false;

	// Special case for root 'child' device (root has no parent dir)
	if (kstrStrcmp("/", device->common.mountPoint)==0)
		return false;

	// Compute dirname for this device's mount point
	char *dirname, *basename;
	kernelFsPathSplitStaticKStr(device->common.mountPoint, &dirname, &basename);

	// Special case for root as parentDir
	if (strcmp(parentDir, "/")==0)
		return (strcmp(dirname, "")==0);

	return (strcmp(dirname, parentDir)==0);
}

bool kernelFsDeviceIsDir(const KernelFsDevice *device) {
	assert(device!=NULL);

	// Only block and directory type devices operate as directories
	switch(device->common.type) {
		case KernelFsDeviceTypeBlock:
			switch(device->block.format) {
				case KernelFsBlockDeviceFormatCustomMiniFs:
					return true;
				break;
				case KernelFsBlockDeviceFormatFlatFile:
					return false;
				break;
				case KernelFsBlockDeviceFormatNB:
					assert(false);
					return false;
				break;
			}
		break;
		case KernelFsDeviceTypeCharacter:
			return false;
		break;
		case KernelFsDeviceTypeDirectory:
			return true;
		break;
		case KernelFsDeviceTypeNB:
			assert(false);
			return false;
		break;
	}

	return false;
}

bool kernelFsDeviceIsDirEmpty(const KernelFsDevice *device) {
	assert(device!=NULL);

	// Only block and directory type devices operate as directories
	switch(device->common.type) {
		case KernelFsDeviceTypeBlock:
			switch(device->block.format) {
				case KernelFsBlockDeviceFormatCustomMiniFs: {
					MiniFs miniFs;
					miniFsMountFast(&miniFs, &kernelFsMiniFsReadWrapper, (device->common.writable ? &kernelFsMiniFsWriteWrapper : NULL), (KernelFsDevice *)device);
					bool res=miniFsIsEmpty(&miniFs);
					miniFsUnmount(&miniFs);
					return res;
				} break;
				case KernelFsBlockDeviceFormatFlatFile:
					// These are not directories
					return false;
				break;
				case KernelFsBlockDeviceFormatNB:
					assert(false);
					return false;
				break;
			}
		break;
		case KernelFsDeviceTypeCharacter:
			return false;
		break;
		case KernelFsDeviceTypeDirectory: {
			// Check explicit virtual devices as children
			for(uint8_t i=0; i<KernelFsDevicesMax; ++i) {
				KernelFsDevice *childDevice=&kernelFsData.devices[i];
				if (childDevice->common.type==KernelFsDeviceTypeNB)
					continue;

				char path[KernelFsPathMax];
				kstrStrcpy(path, device->common.mountPoint);
				if (kernelFsDeviceIsChildOfPath(childDevice, path))
					// Not empty
					return false;
			}

			// Empty
			return true;
		} break;
		case KernelFsDeviceTypeNB:
			assert(false);
			return false;
		break;
	}

	return false;
}

uint16_t kernelFsMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData) {
	assert(data!=NULL);
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	return kernelFsDeviceInvokeFunctorBlockRead(device, data, len, addr);
}

uint16_t kernelFsMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData) {
	assert(data!=NULL);
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatCustomMiniFs);
	assert(device->common.writable);

	return kernelFsDeviceInvokeFunctorBlockwrite(device, data, len, addr);
}

bool kernelFsDeviceInvokeFunctorCommonFlush(KernelFsDevice *device) {
	return (bool)device->common.functor(KernelFsDeviceFunctorTypeCommonFlush, device->common.userData, NULL, 0, 0);
}

int16_t kernelFsDeviceInvokeFunctorCharacterRead(KernelFsDevice *device) {
	return (int16_t)device->common.functor(KernelFsDeviceFunctorTypeCharacterRead, device->common.userData, NULL, 0, 0);
}

bool kernelFsDeviceInvokeFunctorCharacterCanRead(KernelFsDevice *device) {
	return (bool)device->common.functor(KernelFsDeviceFunctorTypeCharacterCanRead, device->common.userData, NULL, 0, 0);
}

KernelFsFileOffset kernelFsDeviceInvokeFunctorCharacterWrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len) {
	return (KernelFsFileOffset)device->common.functor(KernelFsDeviceFunctorTypeCharacterWrite, device->common.userData, (uint8_t *)data, len, 0);
}

bool kernelFsDeviceInvokeFunctorCharacterCanWrite(KernelFsDevice *device) {
	return (bool)device->common.functor(KernelFsDeviceFunctorTypeCharacterCanWrite, device->common.userData, NULL, 0, 0);
}

KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockRead(KernelFsDevice *device, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	return (KernelFsFileOffset)device->common.functor(KernelFsDeviceFunctorTypeBlockRead, device->common.userData, data, len, addr);
}

KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockwrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	return (KernelFsFileOffset)device->common.functor(KernelFsDeviceFunctorTypeBlockWrite, device->common.userData, (uint8_t *)data, len, addr);
}

uint8_t kernelFsFdPathSpareMake(KernelFsFdMode mode, unsigned refCount) {
	if (mode==KernelFsFdModeNone || mode>=KernelFsFdModeMax)
		return 0;
	if (refCount>=KernelFsFdRefCountMax)
		return 0;
	return (((uint8_t)refCount)<<KernelFsFdModeBits)|mode;
}

KernelFsFdMode kernelFsFdPathSpareGetMode(uint8_t spare) {
	return (spare & (KernelFsFdModeMax-1));
}

unsigned kernelFsFdPathSpareGetRefCount(uint8_t spare) {
	return (spare>>KernelFsFdModeBits);
}

uint8_t kernelFsGetFileSpare(KernelFsFd fd) {
	assert(fd<KernelFsFdMax);

	// Fd not open?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Extract spare bits from path str
	return kstrGetSpare(kernelFsData.fdt[fd].path);
}

void kernelFsSetFileSpare(KernelFsFd fd, uint8_t spare) {
	assert(fd<KernelFsFdMax);
	assert(spare<((1u)<<KStrSpareBits));

	// Fd not open?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return;

	// Set spare bits in path str
	kstrSetSpare(&kernelFsData.fdt[fd].path, spare);
}
