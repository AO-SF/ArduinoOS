#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#else
#include <unistd.h>
#endif

#include "fat.h"
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
bool kernelFsPathIsDeviceKStr(KStr path);
bool kernelFsFileCanOpenMany(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPath(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPathKStr(KStr path);
KernelFsDevice *kernelFsGetDeviceFromPathRecursive(const char *path, char **subPath); // if a device is found and subPath is non-NULL, then *subPath is set to point into path after the device's mount point
KernelFsDevice *kernelFsGetDeviceFromPathRecursiveKStr(KStr path, KStr *subPath); // if a device is found and subPath is non-NULL, then *subPath is set to point into path after the device's mount point
KernelFsDeviceIndex kernelFsGetDeviceIndexFromDevice(const KernelFsDevice *device);

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsDeviceType type, bool writable);
void kernelFsRemoveDeviceFileRaw(KernelFsDevice *device);

bool kernelFsDeviceIsChildOfPath(KernelFsDevice *device, const char *parentDir);

// In following functions subPath is relative to the device in question.
// E.g. if device is mounted to '/media/sd' and subPath is 'folder123' then function will query full path '/media/sd/folder123'.
// If subPath is NULL or an empty string then queries device's mount point.
bool kernelFsDeviceIsDir(const KernelFsDevice *device, const char *subPath);
bool kernelFsDeviceIsDirEmpty(const KernelFsDevice *device, const char *subPath);

bool kernelFsDeviceInvokeFunctorCommonFlush(KernelFsDevice *device);

int16_t kernelFsDeviceInvokeFunctorCharacterRead(KernelFsDevice *device);
bool kernelFsDeviceInvokeFunctorCharacterCanRead(KernelFsDevice *device);
KernelFsFileOffset kernelFsDeviceInvokeFunctorCharacterWrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len);
bool kernelFsDeviceInvokeFunctorCharacterCanWrite(KernelFsDevice *device);

KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockRead(KernelFsDevice *device, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockWrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);

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

// These two functions can be passed to the miniFsMount functions to allow reading/writing a MiniFs volume in an open file,
// with the KernelFsDevice pointer passed as the userData field
uint16_t kernelFsDeviceMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t kernelFsDeviceMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

// These two functions can be passed to the fatMount functions to allow reading/writing a FAT volume in an open file,
// with the KernelFsDevice pointer passed as the userData field
uint32_t kernelFsFatReadWrapper(uint32_t addr, uint8_t *data, uint32_t len, void *userData);
uint32_t kernelFsFatWriteWrapper(uint32_t addr, const uint8_t *data, uint32_t len, void *userData);

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
			if (!miniFsMountSafe(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), device))
				goto error;
			miniFsUnmount(&miniFs);
		} break;
		case KernelFsBlockDeviceFormatFlatFile:
		break;
		case KernelFsBlockDeviceFormatFat: {
			Fat fat;
			if (!fatMountSafe(&fat, &kernelFsFatReadWrapper, (writable ? &kernelFsFatWriteWrapper : NULL), device))
				goto error;
			fatUnmount(&fat);
		} break;
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

bool kernelFsUpdateBlockDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, bool writable) {
	assert(!kstrIsNull(mountPoint));
	assert(functor!=NULL);

	// Find device
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(mountPoint);
	if (device==NULL)
		return false;

	// Update device fields
	KernelFsDevice oldDevice=*device;

	device->common.functor=functor;
	device->common.userData=userData;
	device->common.type=KernelFsDeviceTypeBlock;
	device->common.writable=writable;

	device->block.format=format;
	device->block.size=size;

	// Attempt to mount
	switch(format) {
		case KernelFsBlockDeviceFormatCustomMiniFs: {
			MiniFs miniFs;
			if (!miniFsMountSafe(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), device))
				goto error;
			miniFsUnmount(&miniFs);
		} break;
		case KernelFsBlockDeviceFormatFat: {
			Fat fat;
			if (!fatMountSafe(&fat, &kernelFsFatReadWrapper, (writable ? &kernelFsFatWriteWrapper : NULL), device))
				goto error;
			fatUnmount(&fat);
		} break;
		case KernelFsBlockDeviceFormatFlatFile:
		break;
		case KernelFsBlockDeviceFormatNB:
			goto error;
		break;
	}

	return true;

	error:

	// Restore device fields
	*device=oldDevice;

	return false;
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

	return kernelFsFileExistsKStr(kstrS((char *)path));
}

bool kernelFsFileExistsKStr(KStr path) {
	assert(!kstrIsNull(path));

	// Check for virtual device path
	if (kernelFsPathIsDeviceKStr(path))
		return true;

	// Find dirname and basename
	char *dirname, *basename;
	kernelFsPathSplitStaticKStr(path, &dirname, &basename);

	// Check for node at dirname
	KernelFsDevice *device=kernelFsGetDeviceFromPath(dirname);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (device->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), device);
						bool res=miniFsFileExists(&miniFs, basename);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFat: {
						Fat fat;
						if (!fatMountFast(&fat, &kernelFsFatReadWrapper, (device->common.writable ? &kernelFsFatWriteWrapper : NULL), device))
							return false;
						bool res=fatFileExists(&fat, kstrS((char *)basename));
						fatUnmount(&fat);

						return res;
					} break;
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

	// Grab owning device
	char *subPath;
	KernelFsDevice *device=kernelFsGetDeviceFromPathRecursive(path, &subPath);
	if (device==NULL)
		return false;

	// Check if this subPath of this device is a directory.
	return kernelFsDeviceIsDir(device, subPath);
}

bool kernelFsFileIsDirEmpty(const char *path) {
	assert(path!=NULL);

	// Grab owning device
	char *subPath;
	KernelFsDevice *device=kernelFsGetDeviceFromPathRecursive(path, &subPath);
	if (device==NULL)
		return false;

	// Check if this subPath of this device is an empty directory.
	return kernelFsDeviceIsDirEmpty(device, subPath);
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
					case KernelFsBlockDeviceFormatFat:
						// These act as directories at the top level (we check below for child)
						return 0;
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
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (parentDevice->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), parentDevice);
						KernelFsFileOffset res=miniFsFileGetLen(&miniFs, basename);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return 0;
					break;
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for Fat file system support
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
							miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, &kernelFsDeviceMiniFsWriteWrapper, device);
							res=miniFsFileCreate(&miniFs, basename, size);
							miniFsUnmount(&miniFs);
						}
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for FAT file system support
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
	if (device!=NULL && kernelFsDeviceIsDir(device, "") && !kernelFsDeviceIsDirEmpty(device, ""))
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
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for FAT file system support
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
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (parentDevice->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsFileDelete(&miniFs, basename);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for FAT file system support
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
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (parentDevice->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsFileResize(&miniFs, basename, newSize);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for FAT file system support
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

	// If file is already open, decide if it can be opened more than once
	if (alreadyOpen && !kernelFsFileCanOpenMany(path))
		return KernelFsFdInvalid;

	// Update file descriptor table.
	kernelFsData.fdt[newFd].path=kstrC(path);
	if (kstrIsNull(kernelFsData.fdt[newFd].path))
		return KernelFsFdInvalid; // Out of memory

	kernelFsSetFileSpare(newFd, kernelFsFdPathSpareMake(mode, 1)); // store mode and refcount=1 in spare bits of path string

	// Grab device index to save doing this repeatedly in the future
	KernelFsDevice *device=kernelFsGetDeviceFromPathRecursive(path, NULL);
	if (device==NULL) {
		// File shouldn't have passed earlier kernelFsFileExists test but this code is here for safety.
		kernelFsData.fdt[newFd].path=kstrNull();
		return KernelFsFdInvalid;
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
						return 0;
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return kernelFsDeviceInvokeFunctorBlockRead(device, data, dataLen, offset);
					break;
					case KernelFsBlockDeviceFormatFat:
						// These act as directories at the top level (we check below for child)
						return 0;
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
		KStr subPath=kstrO(&kernelFsData.fdt[fd].path, kstrStrlen(device->common.mountPoint)+1); // +1 to skip '/' also

		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						if (offset>=UINT16_MAX)
							return 0;
						if (dataLen>=UINT16_MAX)
							dataLen=UINT16_MAX;
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (device->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), device);
						uint16_t read=miniFsFileReadKStr(&miniFs, subPath, offset, data, dataLen);
						miniFsUnmount(&miniFs);
						return read;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return 0;
					break;
					case KernelFsBlockDeviceFormatFat: {
						Fat fat;
						if (!fatMountFast(&fat, &kernelFsFatReadWrapper, (device->common.writable ? &kernelFsFatWriteWrapper : NULL), device))
							return 0;
						uint16_t read=fatFileRead(&fat, subPath, offset, data, dataLen);
						fatUnmount(&fat);

						return read;
					} break;
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
						return kernelFsDeviceInvokeFunctorBlockWrite(device, data, dataLen, offset);
					break;
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for FAT file system support
						return 0;
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
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, &kernelFsDeviceMiniFsWriteWrapper, device);
						KernelFsFileOffset res=miniFsFileWrite(&miniFs, basename, offset, data, dataLen);
						miniFsUnmount(&miniFs);
						return res;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return 0;
					break;
					case KernelFsBlockDeviceFormatFat:
						// TODO: this for FAT file system support
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

	// Is this a virtual device file or a child of one?
	KernelFsDevice *device=&kernelFsData.devices[kernelFsData.fdt[fd].deviceIndex];
	if (kstrDoubleStrcmp(kernelFsData.fdt[fd].path, device->common.mountPoint)==0) {
		assert(device==kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		// This fd IS the device cached in the fdt (rather than a child of it)
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						MiniFs miniFs;
						miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (device->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), device);

						KernelFsFd j=0;
						for(KernelFsFd i=0; i<MINIFSMAXFILES; ++i) {
							kstrStrcpy(childPath, kernelFsData.fdt[fd].path);
							strcat(childPath, "/");
							bool res=miniFsGetChildN(&miniFs, i, childPath+strlen(childPath));
							if (!res)
								continue;
							if (j==childNum) {
								miniFsUnmount(&miniFs);
								return true;
							}
							++j;
						}

						miniFsUnmount(&miniFs);
						return false;
					} break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFat: {
						Fat fat;
						if (!fatMountFast(&fat, &kernelFsFatReadWrapper, (device->common.writable ? &kernelFsFatWriteWrapper : NULL), device))
							return false;

						kstrStrcpy(childPath, kernelFsData.fdt[fd].path);
						strcat(childPath, "/");

						bool res=fatDirGetChildN(&fat, kstrS((char *)""), childNum, childPath+strlen(childPath));

						fatUnmount(&fat);

						return res;
					} break;
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
	} else {
		assert(device!=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path));

		// This fd is a child of the device cached in the fdt
		KStr subPath=kstrO(&kernelFsData.fdt[fd].path, kstrStrlen(device->common.mountPoint)+1); // +1 to skip '/' also

		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// MiniFs volumes do not support sub-directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						// These are not directories
						return false;
					break;
					case KernelFsBlockDeviceFormatFat: {
						Fat fat;
						if (!fatMountFast(&fat, &kernelFsFatReadWrapper, (device->common.writable ? &kernelFsFatWriteWrapper : NULL), device))
							return false;

						kstrStrcpy(childPath, kernelFsData.fdt[fd].path);
						strcat(childPath, "/");

						bool res=fatDirGetChildN(&fat, subPath, childNum, childPath+strlen(childPath));

						fatUnmount(&fat);

						return res;
					} break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return false;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// These are files and thus cannot contain other files
				return false;
			break;
			case KernelFsDeviceTypeDirectory:
				// Device directories can only contain other virtual devices (which are handled above)
				return false;
			break;
			case KernelFsDeviceTypeNB:
				assert(false);
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

	bool change;
	do {
		char *c;
		change=false;

		// Replace '/x/../' with '/', unless x is also ..
		c=path;
		while((c=strstr(c, "/.."))!=NULL) {
			// Check for either '..' at end of string, or full '/../' part
			if (strcmp(c, "/..")!=0 && strncmp(c, "/../", 4)!=0)
				continue;

			// Look for last slash before this
			char *d;
			for(d=c-1; d>=path; --d) {
				if (*d=='/')
					break;
			}

			// Cannot simplify double instance ('/../../')
			if (d>=path && strncmp(d, "/../", 4)!=0) {
				change=true;

				d+=(strcmp(c, "/..")==0); // keep initial slash in the case where the final '..' is at the end of the string (without a final '/') - otherwise e.g. '/bin/..' would normalise to an empty string rather than '/'
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

uint16_t kernelFsFdMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData) {
	assert(data!=NULL);
	assert(userData!=NULL);

	KernelFsFd fd=(KernelFsFd)(uintptr_t)userData;
	return kernelFsFileReadOffset(fd, addr, data, len);
}

uint16_t kernelFsFdMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData) {
	assert(data!=NULL);
	assert(userData!=NULL);

	KernelFsFd fd=(KernelFsFd)(uintptr_t)userData;
	return kernelFsFileWriteOffset(fd, addr, data, len);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path) {
	assert(path!=NULL);

	return (kernelFsGetDeviceFromPath(path)!=NULL);
}

bool kernelFsPathIsDeviceKStr(KStr path) {
	return (kernelFsGetDeviceFromPathKStr(path)!=NULL);
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
					case KernelFsBlockDeviceFormatFat:
						return !parentDevice->common.writable;
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

KernelFsDevice *kernelFsGetDeviceFromPathRecursive(const char *path, char **subPath) {
	assert(path!=NULL);

	// Loop over devices looking for nearest parent/exact match for given path.
	unsigned pathLen=strlen(path);

	unsigned bestMatchLen=0;
	KernelFsDevice *bestDevice=NULL;

	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];

		// Skip unused device entries
		if (device->common.type==KernelFsDeviceTypeNB)
			continue;

		// Check if we have a new best device
		unsigned matchLen=kstrMatchLen(path, device->common.mountPoint);
		if (matchLen>bestMatchLen) {
			unsigned deviceMountPointLen=kstrStrlen(device->common.mountPoint);

			// Check if this device is a child of the file in the path (rather than the file's parent device)
			if (pathLen<deviceMountPointLen)
				continue;

			// Update best
			bestMatchLen=matchLen;
			bestDevice=device;

			// Exact match?
			if (pathLen==deviceMountPointLen)
				break;
		}
	}

	if (subPath!=NULL) {
		*subPath=((char *)path)+kstrStrlen(bestDevice->common.mountPoint);
		if ((*subPath)[0]=='/')
			++*subPath;
	}

	return bestDevice;
}

KernelFsDevice *kernelFsGetDeviceFromPathRecursiveKStr(KStr path, KStr *subPath) {
	assert(!kstrIsNull(path));

	// Loop over devices looking for nearest parent/exact match for given path.
	unsigned pathLen=kstrStrlen(path);

	unsigned bestMatchLen=0;
	KernelFsDevice *bestDevice=NULL;

	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];

		// Skip unused device entries
		if (device->common.type==KernelFsDeviceTypeNB)
			continue;

		// Check if we have a new best device
		unsigned matchLen=kstrDoubleMatchLen(path, device->common.mountPoint);
		if (matchLen>bestMatchLen) {
			unsigned deviceMountPointLen=kstrStrlen(device->common.mountPoint);

			// Check if this device is a child of the file in the path (rather than the file's parent device)
			if (pathLen<deviceMountPointLen)
				continue;

			// Update best
			bestMatchLen=matchLen;
			bestDevice=device;

			// Exact match?
			if (pathLen==deviceMountPointLen)
				break;
		}
	}

	if (subPath!=NULL) {
		unsigned offset=kstrStrlen(bestDevice->common.mountPoint);
		if (kstrGetChar(path, offset)=='/')
			++offset;
		*subPath=kstrO(&path, offset);
	}

	return bestDevice;
}

KernelFsDeviceIndex kernelFsGetDeviceIndexFromDevice(const KernelFsDevice *device) {
	if (device==NULL)
		return KernelFsDevicesMax;
	return (((const uint8_t *)device)-((const uint8_t *)kernelFsData.devices))/sizeof(KernelFsDevice);
}

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, KernelFsDeviceFunctor *functor, void *userData, KernelFsDeviceType type, bool writable) {
	assert(!kstrIsNull(mountPoint));
	assert(type<KernelFsDeviceTypeNB);

	// Ensure this file does not already exist
	if (kernelFsFileExistsKStr(mountPoint))
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

bool kernelFsDeviceIsDir(const KernelFsDevice *device, const char *subPath) {
	assert(device!=NULL);

	bool isRoot=(subPath==NULL || subPath[0]=='\0');

	// Only block and directory type devices operate as directories
	switch(device->common.type) {
		case KernelFsDeviceTypeBlock:
			switch(device->block.format) {
				case KernelFsBlockDeviceFormatCustomMiniFs:
					// Device itself is a directory but such volumes cannot contain sub-directories
					return isRoot;
				break;
				case KernelFsBlockDeviceFormatFat: {
					// Device itself is always a directory
					if (isRoot)
						return true;

					// If a child of this device then need to do a more thorough check
					Fat fat;
					if (!fatMountFast(&fat, &kernelFsFatReadWrapper, (device->common.writable ? &kernelFsFatWriteWrapper : NULL), (void *)device))
						return false;
					bool res=fatIsDir(&fat, subPath);
					fatUnmount(&fat);
					return res;
				} break;
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
			// Device itself is a directory but child cannot be (given we didn't find a beter matching device)
			return isRoot;
		break;
		case KernelFsDeviceTypeNB:
			assert(false);
			return false;
		break;
	}

	return false;
}

bool kernelFsDeviceIsDirEmpty(const KernelFsDevice *device, const char *subPath) {
	assert(device!=NULL);

	bool isRoot=(subPath==NULL || subPath[0]=='\0');

	// Only block and directory type devices operate as directories
	switch(device->common.type) {
		case KernelFsDeviceTypeBlock:
			switch(device->block.format) {
				case KernelFsBlockDeviceFormatCustomMiniFs: {
					if (!isRoot)
						return false;

					MiniFs miniFs;
					miniFsMountFast(&miniFs, &kernelFsDeviceMiniFsReadWrapper, (device->common.writable ? &kernelFsDeviceMiniFsWriteWrapper : NULL), (KernelFsDevice *)device);
					bool res=miniFsIsEmpty(&miniFs);
					miniFsUnmount(&miniFs);
					return res;
				} break;
				case KernelFsBlockDeviceFormatFlatFile:
					// These are not directories
					return false;
				break;
				case KernelFsBlockDeviceFormatFat: {
					Fat fat;
					if (!fatMountFast(&fat, &kernelFsFatReadWrapper, (device->common.writable ? &kernelFsFatWriteWrapper : NULL), (KernelFsDevice *)device))
						return false;
					bool res=fatDirIsEmpty(&fat, kstrS((char *)subPath));
					fatUnmount(&fat);

					return res;
				} break;
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
			if (!isRoot)
				return false;

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

KernelFsFileOffset kernelFsDeviceInvokeFunctorBlockWrite(KernelFsDevice *device, const uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
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

uint16_t kernelFsDeviceMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData) {
	assert(data!=NULL);
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	return kernelFsDeviceInvokeFunctorBlockRead(device, data, len, addr);
}

uint16_t kernelFsDeviceMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData) {
	assert(data!=NULL);
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatCustomMiniFs);
	assert(device->common.writable);

	return kernelFsDeviceInvokeFunctorBlockWrite(device, data, len, addr);
}

uint32_t kernelFsFatReadWrapper(uint32_t addr, uint8_t *data, uint32_t len, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatFat);

	return kernelFsDeviceInvokeFunctorBlockRead(device, data, len, addr);
}

uint32_t kernelFsFatWriteWrapper(uint32_t addr, const uint8_t *data, uint32_t len, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatFat);
	assert(device->common.writable);

	return kernelFsDeviceInvokeFunctorBlockWrite(device, data, len, addr);
}
