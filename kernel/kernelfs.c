#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kernelfs.h"
#include "minifs.h"

#define KernelFsDevicesMax 32

typedef enum {
	KernelFsDeviceTypeBlock,
	KernelFsDeviceTypeCharacter,
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
	KernelFsDeviceType type;
	char *mountPoint;
	union {
		KernelFsDeviceBlock block;
		KernelFsDeviceCharacter character;
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
	// TODO: this
	return false;
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

	// TODO: Check for standard files/directories

	return false;
}

bool kernelFsFileCreate(const char *path) {
	// TODO: this
	return false;
}

bool kernelFsFileDelete(const char *path) {
	// TODO: this
	return false;
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

KernelFsFileOffset kernelFsFileRead(KernelFsFd fd, uint8_t *data, KernelFsFileOffset dataLen) {
	// Invalid fd?
	if (kernelFsData.fdt[fd]==NULL)
		return 0;

	// Is this a virtual device file?
	KernelFsDevice *device=kernelFsGetDeviceFromPath(kernelFsData.fdt[fd]);
	if (device!=NULL) {
		switch(device->type) {
			case KernelFsDeviceTypeBlock:
				// TODO: this
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
			case KernelFsDeviceTypeNB:
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
				// TODO: this
			break;
			case KernelFsDeviceTypeCharacter: {
				KernelFsFileOffset written;
				for(written=0; written<dataLen; ++written)
					if (!device->d.character.writeFunctor(data[written]))
						break;
				return written;
			} break;
			case KernelFsDeviceTypeNB:
			break;
		}
	}

	// Handle standard files.
	// TODO: this

	return 0;
}

bool kernelFsDirectoryGetChild(KernelFsFd fd, unsigned childNum, char childPath[KernelPathMax]) {
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
