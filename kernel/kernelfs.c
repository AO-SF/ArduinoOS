#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kernelfs.h"
#include "minifs.h"

#define KernelFsDevicesMax 32

typedef enum {
	KernelFsDeviceTypeBlock,
	KernelFsDeviceTypeCharacter,
	KernelFsDeviceTypeDirectory,
	KernelFsDeviceTypeNB,
} KernelFsDeviceType;

typedef struct {
	KernelFsCharacterDeviceReadFunctor *readFunctor;
	KernelFsCharacterDeviceWriteFunctor *writeFunctor;
} KernelFsDeviceCharacter;

typedef struct {
	MiniFs miniFs;
} KernelFsDeviceBlockCustomMiniFs;

typedef struct {
	KernelFsBlockDeviceFormat format;
	KernelFsBlockDeviceReadFunctor *readFunctor;
	KernelFsBlockDeviceWriteFunctor *writeFunctor;
	union {
		KernelFsDeviceBlockCustomMiniFs customMiniFs;
	} d;
} KernelFsDeviceBlock;

typedef struct {
	KernelFsDirectoryDeviceGetChildFunctor *getChildFunctor;
} KernelFsDeviceDirectory;

typedef struct {
	KernelFsDeviceType type;
	char *mountPoint;
	union {
		KernelFsDeviceBlock block;
		KernelFsDeviceCharacter character;
		KernelFsDeviceDirectory directory;
	} d;
} KernelFsDevice;

typedef struct {
	char *fdt[KernelFsFdMax];

	KernelFsDevice devices[KernelFsDevicesMax];
} KernelFsData;

KernelFsData kernelFsData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path);
KernelFsDevice *kernelFsGetDeviceFromPath(const char *path);

KernelFsDevice *kernelFsAddDeviceFile(const char *mountPoint, KernelFsDeviceType type);

uint8_t kernelFsMiniFsReadWrapper(uint16_t addr, void *userData);
void kernelFsMiniFsWriteWrapper(uint16_t addr, uint8_t value, void *userData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void kernelFsInit(void) {
	// Clear file descriptor table
	for(int i=0; i<KernelFsFdMax; ++i)
		kernelFsData.fdt[i]=NULL;

	// Clear virtual device array
	for(int i=0; i<KernelFsDevicesMax; ++i)
		kernelFsData.devices[i].type=KernelFsDeviceTypeNB;
}

void kernelFsQuit(void) {
	// Free memory used in file descriptor table.
	for(int i=0; i<KernelFsFdMax; ++i) {
		free(kernelFsData.fdt[i]);
		kernelFsData.fdt[i]=NULL;
	}

	// Free virtual device array
	for(int i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type==KernelFsDeviceTypeNB)
			continue;
		free(device->mountPoint);
		device->type=KernelFsDeviceTypeNB;
	}
}

bool kernelFsAddCharacterDeviceFile(const char *mountPoint, KernelFsCharacterDeviceReadFunctor *readFunctor, KernelFsCharacterDeviceWriteFunctor *writeFunctor) {
	assert(mountPoint!=NULL);
	assert(readFunctor!=NULL);
	assert(writeFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, KernelFsDeviceTypeCharacter);
	if (device==NULL)
		return false;

	// Set specific fields.
	device->d.character.readFunctor=readFunctor;
	device->d.character.writeFunctor=writeFunctor;

	return true;
}

bool kernelFsAddDirectoryDeviceFile(const char *mountPoint, KernelFsDirectoryDeviceGetChildFunctor *getChildFunctor) {
	assert(mountPoint!=NULL);
	assert(getChildFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, KernelFsDeviceTypeDirectory);
	if (device==NULL)
		return false;

	// Set specific fields.
	device->d.directory.getChildFunctor=getChildFunctor;

	return true;
}

bool kernelFsAddBlockDeviceFile(const char *mountPoint, KernelFsBlockDeviceFormat format, KernelFsFileOffset size, KernelFsBlockDeviceReadFunctor *readFunctor, KernelFsBlockDeviceWriteFunctor *writeFunctor) {
	assert(mountPoint!=NULL);
	assert(readFunctor!=NULL);

	// Check mountPoint and attempt to add to device table.
	KernelFsDevice *device=kernelFsAddDeviceFile(mountPoint, KernelFsDeviceTypeBlock);
	if (device==NULL)
		return false;
	device->d.block.format=KernelFsBlockDeviceFormatCustomMiniFs;
	device->d.block.readFunctor=readFunctor;
	device->d.block.writeFunctor=writeFunctor;

	// Attempt to mount
	switch(format) {
		case KernelFsBlockDeviceFormatCustomMiniFs:
			if (!miniFsMountSafe(&device->d.block.d.customMiniFs.miniFs, &kernelFsMiniFsReadWrapper, &kernelFsMiniFsWriteWrapper, device))
				goto error;
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
	char modPath[256]; // TODO: better
	strcpy(modPath, path);
	char *dirname, *basename;
	kernelFsPathSplit(modPath, &dirname, &basename);

	// Check for node at dirname
	KernelFsDevice *device=kernelFsGetDeviceFromPath(dirname);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsFileExists(&device->d.block.d.customMiniFs.miniFs, basename);
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
				// TODO: this
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

bool kernelFsFileCreate(const char *path) {
	// Find dirname and basename
	char modPath[256]; // TODO: better
	strcpy(modPath, path);
	char *dirname, *basename;
	kernelFsPathSplit(modPath, &dirname, &basename);

	// Check for node at dirname
	KernelFsDevice *device=kernelFsGetDeviceFromPath(dirname);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						// In theory we can create files on a MiniFs if it is not mounted read only
						uint16_t initialSize=32; // TODO: think about this
						return miniFsFileCreate(&device->d.block.d.customMiniFs.miniFs, basename, initialSize);
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

void kernelFsFileDelete(const char *path) {
	// TODO: this
}

KernelFsFd kernelFsFileOpen(const char *path) {
	// Check if this file is already open and also look for an empty slot to use if not.
	KernelFsFd newFd=KernelFsFdInvalid;
	for(int i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (kernelFsData.fdt[i]==NULL)
			newFd=i; // If we suceed we can use this slot
		else if (strcmp(path, kernelFsData.fdt[i])==0)
			return KernelFsFdInvalid; // File is already open
	}

	// Check file exists.
	if (!kernelFsFileExists(path))
		return KernelFsFdInvalid;

	// Update file descriptor table.
	kernelFsData.fdt[newFd]=malloc(strlen(path)+1);
	if (kernelFsData.fdt[newFd]==NULL)
		return KernelFsFdInvalid; // Out of memory

	strcpy(kernelFsData.fdt[newFd], path);

	return newFd;
}

void kernelFsFileClose(KernelFsFd fd) {
	// Clear from file descriptor table.
	free(kernelFsData.fdt[fd]);
	kernelFsData.fdt[fd]=NULL;
}

const char *kernelFsGetFilePath(KernelFsFd fd) {
	return kernelFsData.fdt[fd];
}

KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kernelFsData.fdt[fd]==NULL)
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				// These act as directories at the top level (we check below for child)
				return 0;
			break;
			case KernelFsDeviceTypeCharacter: {
				KernelFsFileOffset read;
				for(read=0; read<dataLen; ++read) {
					int c=device->d.character.readFunctor();
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
	const char *path=kernelFsGetFilePath(fd);

	char modPath[256]; // TODO: better
	strcpy(modPath, path);
	char *dirname, *basename;
	kernelFsPathSplit(modPath, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFileOffset i;
						for(i=0; i<dataLen; ++i) {
							int res=miniFsFileRead(&parentDevice->d.block.d.customMiniFs.miniFs, basename, i);
							if (res==-1)
								break;
							data[i]=res;
						}
						return i;
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

	// Handle standard files.
	// TODO: this

	return 0;
}

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kernelFsData.fdt[fd]==NULL)
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				// These act as directories at the top level (we check below for child)
				return 0;
			break;
			case KernelFsDeviceTypeCharacter: {
				KernelFsFileOffset written;
				for(written=0; written<dataLen; ++written)
					if (!device->d.character.writeFunctor(data[written]))
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
	const char *path=kernelFsGetFilePath(fd);

	char modPath[256]; // TODO: better
	strcpy(modPath, path);
	char *dirname, *basename;
	kernelFsPathSplit(modPath, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						KernelFsFileOffset i;
						for(i=0; i<dataLen; ++i) {
							if (!miniFsFileWrite(&parentDevice->d.block.d.customMiniFs.miniFs, basename, i, data[i]))
								break;
						}
						return i;
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

	// Handle standard files.
	// TODO: this

	return 0;
}

bool kernelFsDirectoryGetChild(KernelFsFd fd, unsigned childNum, char childPath[KernelFsPathMax]) {
	// Invalid fd?
	if (kernelFsData.fdt[fd]==NULL)
		return false;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				switch(device->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs: {
						int j=0;
						for(int i=0; i<MINIFSMAXFILES; ++i) {
							sprintf(childPath, "%s/", kernelFsData.fdt[fd]);
							if (!miniFsGetChildN(&device->d.block.d.customMiniFs.miniFs, i, childPath+strlen(childPath)))
								continue;
							if (j==childNum)
								return true;
							++j;
						}
						return false;
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
			case KernelFsDeviceTypeDirectory:
				return device->d.directory.getChildFunctor(childNum, childPath);
			break;
			case KernelFsDeviceTypeNB:
			break;
		}
	}

	// Handle standard files.
	// TODO: this

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
	if (!kernelFsPathIsValid(path))
		return;

	// TODO: this (e.g. replace '//' with '/')
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

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool kernelFsPathIsDevice(const char *path) {
	return (kernelFsGetDeviceFromPath(path)!=NULL);
}

KernelFsDevice *kernelFsGetDeviceFromPath(const char *path) {
	for(int i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type!=KernelFsDeviceTypeNB && strcmp(path, device->mountPoint)==0)
			return device;
	}
	return NULL;
}

KernelFsDevice *kernelFsAddDeviceFile(const char *mountPoint, KernelFsDeviceType type) {
	assert(mountPoint!=NULL);
	assert(type<KernelFsDeviceTypeNB);

	// Ensure this file does not already exist
	if (kernelFsFileExists(mountPoint))
		return NULL;

	// Ensure the parent directory exists.
	// TODO: this

	// Look for an empty slot in the device table
	for(int i=0; i<KernelFsDevicesMax; ++i) {
		KernelFsDevice *device=&kernelFsData.devices[i];
		if (device->type!=KernelFsDeviceTypeNB)
			continue;

		device->mountPoint=malloc(strlen(mountPoint)+1);
		if (device->mountPoint==NULL)
			return NULL;
		strcpy(device->mountPoint, mountPoint);
		device->type=type;

		return device;
	}

	return NULL;
}

uint8_t kernelFsMiniFsReadWrapper(uint16_t addr, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->type==KernelFsDeviceTypeBlock);
	assert(device->d.block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	int c=device->d.block.readFunctor(addr);
	if (c==-1)
		c=255; // TODO: think about this

	return c;
}

void kernelFsMiniFsWriteWrapper(uint16_t addr, uint8_t value, void *userData) {
	assert(userData!=NULL);

	KernelFsDevice *device=(KernelFsDevice *)userData;
	assert(device->type==KernelFsDeviceTypeBlock);
	assert(device->d.block.format==KernelFsBlockDeviceFormatCustomMiniFs);

	if (device->d.block.writeFunctor==NULL)
		return; // TODO: think about this

	device->d.block.writeFunctor(addr, value); // TODO: think about return
}
