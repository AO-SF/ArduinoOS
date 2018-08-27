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
#include "wrapper.h"

#define KernelFsDevicesMax 64
typedef uint8_t KernelFsDeviceIndex;

typedef uint8_t KernelFsDeviceType;
#define KernelFsDeviceTypeBlock 0
#define KernelFsDeviceTypeCharacter 1
#define KernelFsDeviceTypeDirectory 2
#define KernelFsDeviceTypeNB 3

typedef struct {
	KernelFsCharacterDeviceReadFunctor *readFunctor;
	KernelFsCharacterDeviceCanReadFunctor *canReadFunctor;
	KernelFsCharacterDeviceWriteFunctor *writeFunctor;
	void *functorUserData;
} KernelFsDeviceCharacter;

typedef struct {
	MiniFs miniFs;
} KernelFsDeviceBlockCustomMiniFs;

typedef struct {
	KernelFsFileOffset size;
	KernelFsBlockDeviceReadFunctor *readFunctor;
	KernelFsBlockDeviceWriteFunctor *writeFunctor;
	void *functorUserData;
	union {
		KernelFsDeviceBlockCustomMiniFs customMiniFs;
	} d;
	KernelFsBlockDeviceFormat format;
} KernelFsDeviceBlock;

typedef struct {
	KStr mountPoint;
	union {
		KernelFsDeviceBlock block;
		KernelFsDeviceCharacter character;
	} d;
	KernelFsDeviceType type;
} KernelFsDevice;

typedef struct {
	KStr fdt[KernelFsFdMax];

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

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, KernelFsDeviceType type);

bool kernelFsDeviceIsChildOfPath(KernelFsDevice *device, const char *parentDir);

uint8_t kernelFsMiniFsReadWrapper(uint16_t addr, void *userData);
void kernelFsMiniFsWriteWrapper(uint16_t addr, uint8_t value, void *userData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void) {
	// Clear file descriptor table
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i)
		kernelFsData.fdt[i]=kstrNull();

	// Clear virtual device array
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i)
		kernelFsData.devices[i].type=KernelFsDeviceTypeNB;
}

void kernelFsQuit(void) {
	// Free memory used in file descriptor table.
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i)
		kstrFree(&kernelFsData.fdt[i]);

	// Free virtual device array
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type==KernelFsDeviceTypeNB)
			continue;
		kstrFree(&device->mountPoint);
		device->type=KernelFsDeviceTypeNB;
	}
}

bool kernelFsAddCharacterDeviceFile(KStr mountPoint, KernelFsCharacterDeviceReadFunctor *readFunctor, KernelFsCharacterDeviceCanReadFunctor *canReadFunctor, KernelFsCharacterDeviceWriteFunctor *writeFunctor, void *functorUserData) {
	assert(!kstrIsNull(mountPoint));
	assert(readFunctor!=NULL);
	assert(canReadFunctor!=NULL);
	assert(writeFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, KernelFsDeviceTypeCharacter);
	if (device==NULL)
		return false;

	// Set specific fields.
	device->d.character.readFunctor=readFunctor;
	device->d.character.canReadFunctor=canReadFunctor;
	device->d.character.writeFunctor=writeFunctor;
	device->d.character.functorUserData=functorUserData;

	return true;
}

bool kernelFsAddDirectoryDeviceFile(KStr mountPoint) {
	assert(!kstrIsNull(mountPoint));

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, KernelFsDeviceTypeDirectory);
	if (device==NULL)
		return false;

	return true;
}

bool kernelFsAddBlockDeviceFile(KStr mountPoint, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, KernelFsBlockDeviceReadFunctor *readFunctor, KernelFsBlockDeviceWriteFunctor *writeFunctor, void *functorUserData) {
	assert(!kstrIsNull(mountPoint));
	assert(readFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, KernelFsDeviceTypeBlock);
	if (device==NULL)
		return false;
	device->d.block.format=format;
	device->d.block.size=size;
	device->d.block.readFunctor=readFunctor;
	device->d.block.writeFunctor=writeFunctor;
	device->d.block.functorUserData=functorUserData;

	// Attempt to mount
	switch(format) {
		case KernelFsBlockDeviceFormatCustomMiniFs:
			if (!miniFsMountSafe(&device->d.block.d.customMiniFs.miniFs, &kernelFsMiniFsReadWrapper, (writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device))
				goto error;
		break;
		case KernelFsBlockDeviceFormatFlatFile:
		break;
		case KernelFsBlockDeviceFormatNB:
			goto error;
		break;
	}

	return true;

	error:

	// TODO: remove device if managed to add

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
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsFileExists(&device->d.block.d.customMiniFs.miniFs, basename);
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

	// TODO: Others

	// No suitable node found
	return false;
}

bool kernelFsFileIsOpen(const char *path) {
	for(KernelFsFd i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (!kstrIsNull(kernelFsData.fdt[i]) && kstrStrcmp(path, kernelFsData.fdt[i])==0)
			return true;
	}
	return false;
}

bool kernelFsFileIsDir(const char *path) {
	// Currently directories can only exist as device files
	KernelFsDevice *device=kernelFsGetDeviceFromPath(path);
	if (device==NULL)
		return false;

	// Only block and directory type devices operate as directories
	switch(device->type) {
		case KernelFsDeviceTypeBlock:
			switch(device->d.block.format) {
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
	switch(device->type) {
		case KernelFsDeviceTypeBlock:
			switch(device->d.block.format) {
				case KernelFsBlockDeviceFormatCustomMiniFs:
					return (miniFsGetChildCount(&device->d.block.d.customMiniFs.miniFs)==0);
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
				if (childDevice->type==KernelFsDeviceTypeNB)
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
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
						return 0;
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return device->d.block.size;
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
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsFileGetLen(&parentDevice->d.block.d.customMiniFs.miniFs, basename);
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
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						// In theory we can create files on a MiniFs if it is not mounted read only
						return miniFsFileCreate(&device->d.block.d.customMiniFs.miniFs, basename, size);
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
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						miniFsUnmount(&device->d.block.d.customMiniFs.miniFs);
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

		kstrFree(&device->mountPoint);
		device->mountPoint=kstrNull();
		device->type=KernelFsDeviceTypeNB;

		return true;
	}

	// Check for being a child of a virtual block device
	char *dirname, *basename;
	kernelFsPathSplitStatic(path, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsFileDelete(&parentDevice->d.block.d.customMiniFs.miniFs, basename);
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
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsFileResize(&parentDevice->d.block.d.customMiniFs.miniFs, basename, newSize);
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

		if (kstrIsNull(kernelFsData.fdt[i]))
			newFd=i; // If we suceed we can use this slot
		else if (kstrStrcmp(path, kernelFsData.fdt[i])==0)
			alreadyOpen=true;
	}

	// If file is already open, decide if it can be openned more than once
	if (alreadyOpen && !kernelFsFileCanOpenMany(path))
		return KernelFsFdInvalid;

	// Update file descriptor table.
	kernelFsData.fdt[newFd]=kstrC(path);
	if (kstrIsNull(kernelFsData.fdt[newFd]))
		return KernelFsFdInvalid; // Out of memory

	return newFd;
}

void kernelFsFileClose(KernelFsFd fd) {
	// Clear from file descriptor table.
	kstrFree(&kernelFsData.fdt[fd]);
}

KStr kernelFsGetFilePath(KernelFsFd fd) {
	return kernelFsData.fdt[fd];
}

KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen) {
	return kernelFsFileReadOffset(fd, 0, data, dataLen, true);
}

KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen, bool block) {
	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd]))
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
					break;
					case KernelFsBlockDeviceFormatFlatFile: {
						KernelFsFileOffset read;
						for(read=0; read<dataLen; ++read) {
							int16_t c=device->d.block.readFunctor(offset+read, device->d.block.functorUserData);
							if (c==-1)
								break;
							data[read]=c;
						}
						return read;
					} break;
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
					if (!block && !device->d.character.canReadFunctor(device->d.character.functorUserData))
						break;
					int16_t c=device->d.character.readFunctor(device->d.character.functorUserData);
					if (c==-1)
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
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFileOffset i;
						for(i=0; i<dataLen; ++i) {
							int16_t res=miniFsFileRead(&parentDevice->d.block.d.customMiniFs.miniFs, basename, offset+i);
							if (res==-1 || res>=256)
								break;
							data[i]=res;
						}
						return i;
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
	if (kstrIsNull(kernelFsData.fdt[fd]))
		return false;

	// Is this a virtual character device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd]);
	if (device!=NULL && device->type==KernelFsDeviceTypeCharacter)
		return device->d.character.canReadFunctor(device->d.character.functorUserData);

	// Otherwise all other file types never block
	return true;
}

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen) {
	return kernelFsFileWriteOffset(fd, 0, data, dataLen);
}

KernelFsFileOffset kernelFsFileWriteOffset(KernelFsFd fd, KernelFsFileOffset offset, const uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kstrIsNull(kernelFsData.fdt[fd]))
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						// These act as directories at the top level (we check below for child)
					break;
					case KernelFsBlockDeviceFormatFlatFile: {
						if (device->d.block.writeFunctor==NULL)
							return 0;

						KernelFsFileOffset written;
						for(written=0; written<dataLen; ++written)
							if (!device->d.block.writeFunctor(offset+written, data[written], device->d.character.functorUserData))
								break;
						return written;
					} break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
						return 0;
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter: {
				// offset is ignored as these are not seekable
				KernelFsFileOffset written;
				for(written=0; written<dataLen; ++written)
					if (!device->d.character.writeFunctor(data[written], device->d.character.functorUserData))
						break;
				return written;
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
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFileOffset i;
						for(i=0; i<dataLen; ++i) {
							if (!miniFsFileWrite(&parentDevice->d.block.d.customMiniFs.miniFs, basename, offset+i, data[i]))
								break;
						}
						return i;
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
	if (kstrIsNull(kernelFsData.fdt[fd]))
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPathKStr(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFd j=0;
						for(KernelFsFd i=0; i<MINIFSMAXFILES; ++i) {
							kstrStrcpy(childPath, kernelFsData.fdt[fd]);
							strcat(childPath, "/");
							if (!miniFsGetChildN(&device->d.block.d.customMiniFs.miniFs, i, childPath+strlen(childPath)))
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
					if (childDevice->type==KernelFsDeviceTypeNB)
						continue;

					kstrStrcpy(childPath, kernelFsData.fdt[fd]); // Borrow childPath as a generic buffer temporarily
					if (kernelFsDeviceIsChildOfPath(childDevice, childPath)) {
						if (foundCount==childNum) {
							kstrStrcpy(childPath, childDevice->mountPoint);
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

		// Replace '/x/../' with '/'
		c=path;
		while((c=strstr(c, "/../"))!=NULL) {
			// Look for last slash before this
			char *d;
			for(d=c-1; d>=path; --d) {
				if (*d=='/')
					break;
			}

			change=true;
			memmove(d, c+3, strlen(c+3)+1);

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
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsGetReadOnly(&device->d.block.d.customMiniFs.miniFs);
					break;
					case KernelFsBlockDeviceFormatFlatFile:
						return (device->d.block.writeFunctor==NULL);
					break;
					case KernelFsBlockDeviceFormatNB:
						assert(false);
					break;
				}
			break;
			case KernelFsDeviceTypeCharacter:
				// TODO: Most should allow this (except e.g. /dev/ttyS0)
				return false;
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
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						if (miniFsGetReadOnly(&parentDevice->d.block.d.customMiniFs.miniFs))
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
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type!=KernelFsDeviceTypeNB && kstrStrcmp(path, device->mountPoint)==0)
			return device;
	}
	return NULL;
}

KernelFsDevice *kernelFsGetDeviceFromPathKStr(KStr path) {
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type!=KernelFsDeviceTypeNB && kstrDoubleStrcmp(path, device->mountPoint)==0)
			return device;
	}
	return NULL;
}

KernelFsDevice *kernelFsAddDeviceFile(KStr mountPoint, KernelFsDeviceType type) {
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
			if (!kernelFsFileExists("/"))
				return NULL;
		} else if (!kernelFsFileExists(dirname))
			return NULL;
	}

	// Look for an empty slot in the device table
	for(KernelFsDeviceIndex i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type!=KernelFsDeviceTypeNB)
			continue;

		device->mountPoint=mountPoint;
		device->type=type;

		return device;
	}

	return NULL;
}

bool kernelFsDeviceIsChildOfPath(KernelFsDevice *device, const char *parentDir) {
	assert(device!=NULL);
	assert(parentDir!=NULL);

	// Invalid path?
	if (!kernelFsPathIsValid(parentDir))
		return false;

	// Special case for root 'child' device (root has no parent dir)
	if (kstrStrcmp("/", device->mountPoint)==0)
		return false;

	// Compute dirname for this device's mount point
	char *dirname, *basename;
	kernelFsPathSplitStaticKStr(device->mountPoint, &dirname, &basename);

	// Special case for root as parentDir
	if (strcmp(parentDir, "/")==0)
		return (strcmp(dirname, "")==0);

	return (strcmp(dirname, parentDir)==0);
}

uint8_t kernelFsMiniFsReadWrapper(uint16_t addr, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->type==KernelFsDeviceTypeBlock);
	assert(device->d.block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	int16_t c=device->d.block.readFunctor(addr, device->d.block.functorUserData);
	if (c==-1) {
		// TODO: think about this
		assert(false);
		c=255;
	}

	return c;
}

void kernelFsMiniFsWriteWrapper(uint16_t addr, uint8_t value, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->type==KernelFsDeviceTypeBlock);
	assert(device->d.block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	if (device->d.block.writeFunctor==NULL) {
		// TODO: think about this
		assert(false);
		return;
	}

	device->d.block.writeFunctor(addr, value, device->d.block.functorUserData); // TODO: think about return
}
