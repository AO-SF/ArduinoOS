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
bool kernelFsFileCanOpenMany(const char *path);
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
			if (!miniFsMountSafe(&device->d.block.d.customMiniFs.miniFs, &kernelFsMiniFsReadWrapper, (writeFunctor!=NULL ? &kernelFsMiniFsWriteWrapper : NULL), device))
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
	char modPath[KernelFsPathMax];
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
				for(uint8_t i=0; ; ++i) {
					char childPath[KernelFsPathMax];
					if (!device->d.directory.getChildFunctor(i, childPath))
						return false;
					if (strcmp(childPath, basename)==0)
						return true;
				}
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
	for(int i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (kernelFsData.fdt[i]!=NULL && strcmp(path, kernelFsData.fdt[i])==0)
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
			char dummyPath[KernelFsPathMax];
			return (!device->d.directory.getChildFunctor(0, dummyPath));
		} break;
		case KernelFsDeviceTypeNB:
			assert(false);
			return false;
		break;
	}

	return false;
}

bool kernelFsFileCreate(const char *path) {
	return kernelFsFileCreateWithSize(path, 0);
}

bool kernelFsFileCreateWithSize(const char *path, KernelFsFileOffset size) {
	// Find dirname and basename
	char modPath[KernelFsPathMax];
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
						return miniFsFileCreate(&device->d.block.d.customMiniFs.miniFs, basename, size);
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
		int slot=device-kernelFsData.devices;
		assert(&kernelFsData.devices[slot]==device);

		free(device->mountPoint);
		device->mountPoint=NULL;
		device->type=KernelFsDeviceTypeNB;

		return true;
	}

	// Check for being a child of a virtual block device
	char modPath[KernelFsPathMax];
	strcpy(modPath, path);
	char *dirname, *basename;
	kernelFsPathSplit(modPath, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						return miniFsFileDelete(&parentDevice->d.block.d.customMiniFs.miniFs, basename);
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
	// TODO: this
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
	for(int i=0; i<KernelFsFdMax; ++i) {
		if (i==KernelFsFdInvalid)
			continue;

		if (kernelFsData.fdt[i]==NULL)
			newFd=i; // If we suceed we can use this slot
		else if (strcmp(path, kernelFsData.fdt[i])==0)
			alreadyOpen=true;
	}

	// If file is already open, decide if it can be openned more than once
	if (alreadyOpen && !kernelFsFileCanOpenMany(path))
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
	return kernelFsFileReadOffset(fd, 0, data, dataLen);
}

KernelFsFileOffset kernelFsFileReadOffset(KernelFsFd fd, KernelFsFileOffset offset, uint8_t *data, KernelFsFileOffset dataLen) {
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
				// offset is ignored as these are not seekable
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

	char modPath[KernelFsPathMax];
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
							int res=miniFsFileRead(&parentDevice->d.block.d.customMiniFs.miniFs, basename, offset+i);
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

	return 0;
}

KernelFsFileOffset kernelFsFileWrite(KernelFsFd fd, const uint8_t *data, KernelFsFileOffset dataLen) {
	return kernelFsFileWriteOffset(fd, 0, data, dataLen);
}

KernelFsFileOffset kernelFsFileWriteOffset(KernelFsFd fd, KernelFsFileOffset offset, const uint8_t *data, KernelFsFileOffset dataLen) {
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
				// offset is ignored as these are not seekable
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

	char modPath[KernelFsPathMax];
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
							if (!miniFsFileWrite(&parentDevice->d.block.d.customMiniFs.miniFs, basename, offset+i, data[i]))
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
						if (miniFsGetReadOnly(&device->d.block.d.customMiniFs.miniFs))
							return true;
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
	char modPath[KernelFsPathMax];
	strcpy(modPath, path);
	char *dirname, *basename;
	kernelFsPathSplit(modPath, &dirname, &basename);

	KernelFsDevice *parentDevice=kernelFsGetDeviceFromPath(dirname);
	if (parentDevice!=NULL) {
		switch(parentDevice->type) {
			case KernelFsDeviceTypeBlock:
				switch(parentDevice->d.block.format) {
					case KernelFsBlockDeviceFormatCustomMiniFs:
						if (miniFsGetReadOnly(&parentDevice->d.block.d.customMiniFs.miniFs))
							return true;
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

	// Ensure the parent directory exists (skipped for root)
	if (strcmp(mountPoint, "/")!=0) {
		char mountPointMod[KernelFsPathMax];
		strcpy(mountPointMod, mountPoint);
		char *dirname, *basename;
		kernelFsPathSplit(mountPointMod, &dirname, &basename);

		if (strlen(dirname)==0) {
			// Special case for files in root directory
			if (!kernelFsFileExists("/"))
				return NULL;
		} else if (!kernelFsFileExists(dirname))
			return NULL;
	}

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

	device->d.block.writeFunctor(addr, value); // TODO: think about return
}
