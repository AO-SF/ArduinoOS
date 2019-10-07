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

STATICASSERT(KernelFsDeviceTypeBits+1+5==8);
typedef struct {
	KStr mountPoint;

	void *userData;

	uint8_t type:KernelFsDeviceTypeBits; // type is KernelFsDeviceType
	uint8_t characterCanOpenManyFlag:1;
	uint8_t reserved:5;

	// Type-specific data follows
} KernelFsDeviceCommon;

typedef struct {
	KernelFsDeviceCommon common;

	KernelFsCharacterDeviceReadFunctor *readFunctor;
	KernelFsCharacterDeviceCanReadFunctor *canReadFunctor;
	KernelFsCharacterDeviceWriteFunctor *writeFunctor;
} KernelFsDeviceCharacter;

typedef struct {
	KernelFsDeviceCommon common;

	KernelFsFileOffset size;
	KernelFsBlockDeviceReadFunctor *readFunctor;
	KernelFsBlockDeviceWriteFunctor *writeFunctor;
	KernelFsBlockDeviceFormat format;
} KernelFsDeviceBlock;

typedef union {
	KernelFsDeviceCommon common; // can always be accessed
	KernelFsDeviceBlock block;
	KernelFsDeviceCharacter character;
} KernelFsDevice;

typedef struct {
	KStr path;
	KernelFsDeviceIndex deviceIndex;
} KernelFsFdtEntry;

typedef struct {
	KernelFsFdtEntry fdt[KernelFsFdMax];

	KernelFsDevice devices[KernelFsDevicesMax];
} KernelFsData;

KernelFsData kernelFsData;

char kernelFsPathSplitStaticBuf[KernelFsPathMax];

MiniFs kernelFsScratchMiniFs;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path);
bool kernelFsFileCanOpenMany(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPath(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPathKStr(KStr path);
KernelFsDeviceIndex kernelFsGetDeviceIndexFromDevice(const KernelFsDevice *device);

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, void *userData, KernelFsDeviceType type);
void kernelFsRemoveDeviceFile(KernelFsDevice *device);

bool kernelFsDeviceIsChildOfPath(KernelFsDevice *device, const char *parentDir);

uint16_t kernelFsMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t kernelFsMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

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

bool kernelFsAddCharacterDeviceFile(KStr mountPoint, KernelFsCharacterDeviceReadFunctor *readFunctor, KernelFsCharacterDeviceCanReadFunctor *canReadFunctor, KernelFsCharacterDeviceWriteFunctor *writeFunctor, bool canOpenMany, void *userData) {
	assert(!kstrIsNull(mountPoint));
	assert(readFunctor!=NULL);
	assert(canReadFunctor!=NULL);
	assert(writeFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, userData, KernelFsDeviceTypeCharacter);
	if (device==NULL)
		return false;

	// Set specific fields.
	device->character.readFunctor=readFunctor;
	device->character.canReadFunctor=canReadFunctor;
	device->character.writeFunctor=writeFunctor;
	device->common.characterCanOpenManyFlag=(canOpenMany || writeFunctor==NULL);

	return true;
}

bool kernelFsAddDirectoryDeviceFile(KStr mountPoint) {
	assert(!kstrIsNull(mountPoint));

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, NULL, KernelFsDeviceTypeDirectory);
	if (device==NULL)
		return false;

	return true;
}

bool kernelFsAddBlockDeviceFile(KStr mountPoint, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, KernelFsBlockDeviceReadFunctor *readFunctor, KernelFsBlockDeviceWriteFunctor *writeFunctor, void *userData) {
	assert(!kstrIsNull(mountPoint));
	assert(readFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, userData, KernelFsDeviceTypeBlock);
	if (device==NULL)
		return false;
	device->block.format=format;
	device->block.size=size;
	device->block.readFunctor=readFunctor;
	device->block.writeFunctor=writeFunctor;

	// Attempt to mount
	switch(format) {
		case KernelFsBlockDeviceFormatCustomMiniFs:
			if (!miniFsMountSafe(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device))
				goto error;
			miniFsUnmount(&kernelFsScratchMiniFs);
		break;
		case KernelFsBlockDeviceFormatFlatFile:
		break;
		case KernelFsBlockDeviceFormatNB:
			goto error;
		break;
	}

	return true;

	error:
	kernelFsRemoveDeviceFile(device);
	return false;
}

bool kernelFsFileExists(const char *path) {
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
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (device->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device);
						bool res=miniFsFileExists(&kernelFsScratchMiniFs, basename);
						miniFsUnmount(&kernelFsScratchMiniFs);
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
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (!kstrIsNull(kernelFsData.fdt[i].path) && kstrStrcmp(path, kernelFsData.fdt[i].path)==0)
			return true;
	}
	return false;
}

bool kernelFsFileIsOpenByFd(KernelFsFd fd) {
	return !kstrIsNull(kernelFsData.fdt[fd].path);
}

bool kernelFsFileIsDir(const char *path) {
	// Currently directories can only exist as device files
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL)
		return false;

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

bool kernelFsFileIsDirEmpty(const char *path) {
	// Currently directories can only exist as device files
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL)
		return false;

	// Only block and directory type devices operate as directories
	switch(device->common.type) {
		case KernelFsDeviceTypeBlock:
			switch(device->block.format) {
				case KernelFsBlockDeviceFormatCustomMiniFs:
					miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (device->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device);
					bool res=miniFsIsEmpty(&kernelFsScratchMiniFs);
					miniFsUnmount(&kernelFsScratchMiniFs);
					return res;
				break;
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

KernelFsFileOffset kernelFsFileGetLen(const char *path) {
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
					case KernelFsBlockDeviceFormatCustomMiniFs:
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (parentDevice->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						KernelFsFileOffset res=miniFsFileGetLen(&kernelFsScratchMiniFs, basename);
						miniFsUnmount(&kernelFsScratchMiniFs);
						return res;
					break;
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
	return kernelFsFileCreateWithSize(path, 0);
}

bool kernelFsFileCreateWithSize(const char *path, KernelFsFileOffset size) {
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
						if (device->block.writeFunctor!=NULL) {
							miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, &kernelFsMiniFsWriteWrapper, device);
							res=miniFsFileCreate(&kernelFsScratchMiniFs, basename, size);
							miniFsUnmount(&kernelFsScratchMiniFs);
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
	// Ensure this file is not open
	if (kernelFsFileIsOpen(path))
		return false;

	// If this is a directory, check if empty
	if (kernelFsFileIsDir(path) && !kernelFsFileIsDirEmpty(path))
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
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
		KernelFsDeviceIndex slot=device-kernelFsData.devices;
		assert(&kernelFsData.devices[slot]==device);
		_unused(slot);

		kstrFree(&device->common.mountPoint);
		device->common.mountPoint=kstrNull();
		device->common.type=KernelFsDeviceTypeNB;

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
					case KernelFsBlockDeviceFormatCustomMiniFs:
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (parentDevice->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsFileDelete(&kernelFsScratchMiniFs, basename);
						miniFsUnmount(&kernelFsScratchMiniFs);
						return res;
					break;
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

bool kernelFsFileResize(const char *path, KernelFsFileOffset newSize) {
	// Ensure this file is not open
	if (kernelFsFileIsOpen(path))
		return false;

	// If this is a directory, cannot resize
	if (kernelFsFileIsDir(path))
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device!=NULL) {
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
					case KernelFsBlockDeviceFormatCustomMiniFs:
						if (newSize>=UINT16_MAX)
							return false; // minifs limits files to 64kb
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (parentDevice->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsFileResize(&kernelFsScratchMiniFs, basename, newSize);
						miniFsUnmount(&kernelFsScratchMiniFs);
						return res;
					break;
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

KernelFsFd kernelFsFileOpen(const char *path) {
	// Not valid path?
	if (!kernelFsPathIsValid(path))
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

void kernelFsFileClose(KernelFsFd fd) {
	// Clear from file descriptor table.
	kstrFree(&kernelFsData.fdt[fd].path);
	kernelFsData.fdt[fd].deviceIndex=KernelFsDevicesMax;
}

KStr kernelFsGetFilePath(KernelFsFd fd) {
	return kernelFsData.fdt[fd].path;
}

KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen) {
	return kernelFsFileReadOffset(fd, 0, data, dataLen, true);
}

KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen, bool block) {
	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return device->block.readFunctor(offset, data, dataLen, device->common.userData);
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
					if (!block && !device->character.canReadFunctor(device->common.userData))
						break;
					int16_t c=device->character.readFunctor(device->common.userData);
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
	}

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStaticKStr(kernelFsGetFilePath(fd), &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						if (offset>=UINT16_MAX)
							return 0;
						if (dataLen>=UINT16_MAX)
							dataLen=UINT16_MAX;
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (parentDevice->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						uint16_t read=miniFsFileRead(&kernelFsScratchMiniFs, basename, offset, data, dataLen);
						miniFsUnmount(&kernelFsScratchMiniFs);
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
	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return false;

	// Is this a virtual character device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd]);
	if (device!=NULL && device->common.type==KernelFsDeviceTypeCharacter)
		return device->character.canReadFunctor(device->common.userData);

	// Otherwise all other file types never block
	return true;
}

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen) {
	return kernelFsFileWriteOffset(fd, 0, data, dataLen);
}

KernelFsFileOffset kernelFsFileWriteOffset(KernelFsFd fd, KernelFsFileOffset offset, const uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
					break;
					case KernelFsBlockDeviceFormatFlatFile: {
						if (device->block.writeFunctor==NULL)
							return 0;

						return device->block.writeFunctor(offset, data, dataLen, device->common.userData);
					} break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter: {
				// offset is ignored as these are not seekable
				return device->character.writeFunctor(data, dataLen, device->common.userData);
			} break;
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
	kernelFsPathSplitStaticKStr(kernelFsGetFilePath(fd), &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						if (offset>=UINT16_MAX)
							return false;
						if (dataLen>=UINT16_MAX)
							dataLen=UINT16_MAX;
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (parentDevice->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						KernelFsFileOffset res=miniFsFileWrite(&kernelFsScratchMiniFs, basename, offset, data, dataLen);
						miniFsUnmount(&kernelFsScratchMiniFs);
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

bool kernelFsDirectoryGetChild(KernelFsFd fd, unsigned childNum, char childPath[KernelFsPathMax]) {
	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd].path))
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd].path);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFd j=0;
						for(KernelFsFd i=0; i<MINIFSMAXFILES; ++i) {
							kstrStrcpy(childPath, kernelFsData.fdt[fd].path);
							strcat(childPath, "/");
							miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (device->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device);
							bool res=miniFsGetChildN(&kernelFsScratchMiniFs, i, childPath+strlen(childPath));
							miniFsUnmount(&kernelFsScratchMiniFs);
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
	// All paths are absolute
	if (path[0]!='/')
		return false;

	// Only '/' root directory can end in a slash
	if (strcmp(path, "/")!=0 && path[strlen(path)-1]=='/')
		return false;

	return true;
}

void kernelFsPathNormalise(char *path) {
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

	// Work backwards looking for final slash
	char *lastSlash=strrchr(path, '/');
	assert(lastSlash!=NULL);
	*lastSlash='\0';
	*basenamePtr=lastSlash+1;
	*dirnamePtr=path;
}

void kernelFsPathSplitStatic(const char *path, char **dirnamePtr, char **basenamePtr) {
	strcpy(kernelFsPathSplitStaticBuf, path);
	kernelFsPathSplit(kernelFsPathSplitStaticBuf, dirnamePtr, basenamePtr);
}

void kernelFsPathSplitStaticKStr(KStr kstr, char **dirnamePtr, char **basenamePtr) {
	kstrStrcpy(kernelFsPathSplitStaticBuf, kstr);
	kernelFsPathSplit(kernelFsPathSplitStaticBuf, dirnamePtr, basenamePtr);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path) {
	return (kernelFsGetDeviceFromPath(path)!=NULL);
}

bool kernelFsFileCanOpenMany(const char *path) {
	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device!=NULL) {
		switch(device->common.type) {
			case KernelFsDeviceTypeBlock:
				switch(device->block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (device->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device);
						bool res=miniFsGetReadOnly(&kernelFsScratchMiniFs);
						miniFsUnmount(&kernelFsScratchMiniFs);
						return res;
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return (device->block.writeFunctor==NULL);
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
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
						miniFsMountFast(&kernelFsScratchMiniFs, &kernelFsMiniFsReadWrapper, (parentDevice->block.writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), parentDevice);
						bool res=miniFsGetReadOnly(&kernelFsScratchMiniFs);
						miniFsUnmount(&kernelFsScratchMiniFs);
						if (res)
							return true;
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

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, void *userData, KernelFsDeviceType type) {
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
		device->common.userData=userData;
		device->common.type=type;

		return device;
	}

	return NULL;
}

void kernelFsRemoveDeviceFile(KernelFsDevice *device) {
	// Does this device file even exist?
	if (device->common.type==KernelFsDeviceTypeNB)
		return;

	// Clear type and free memory
	device->common.type=KernelFsDeviceTypeNB;
	kstrFree(&device->common.mountPoint);
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

uint16_t kernelFsMiniFsReadWrapper(uint16_t addr, uint8_t *data, uint16_t len, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	return device->block.readFunctor(addr, data, len, device->common.userData);
}

uint16_t kernelFsMiniFsWriteWrapper(uint16_t addr, const uint8_t *data, uint16_t len, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->common.type==KernelFsDeviceTypeBlock);
	assert(device->block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	if (device->block.writeFunctor==NULL)
		return 0;

	return device->block.writeFunctor(addr, data, len, device->common.userData);
}
