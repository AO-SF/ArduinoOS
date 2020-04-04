#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernelmount.h"
#include "log.h"
#include "ptable.h"

typedef struct {
	uint8_t format;
	KernelFsFd fd;
} KernelMountDevice;

#define kernelMountedDevicesMax 32
uint8_t kernelMountedDevicesNext=0;
KernelMountDevice kernelMountedDevices[kernelMountedDevicesMax];

uint32_t kernelMountFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
bool kernelMountFlushFunctor(void *userData);
KernelFsFileOffset kernelMountReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelMountWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);

const KernelMountDevice *kernelMountGetDeviceFromFd(KernelFsFd fd);

bool kernelMount(KernelMountFormat format, const char *devicePath, const char *dirPath) {
	// Check paths are valid
	if (!kernelFsPathIsValid(devicePath) || !kernelFsPathIsValid(dirPath)) {
		kernelLog(LogTypeWarning, kstrP("could not mount - bad path(s) (format=%u, devicePath='%s', dirPath='%s')\n"), format, devicePath, dirPath);
		return false;
	}

	// Check we have space to mount another device
	if (kernelMountedDevicesNext>=kernelMountedDevicesMax) {
		kernelLog(LogTypeWarning, kstrP("could not mount - already have maximum number of devices mounted (%u) (format=%u, devicePath='%s', dirPath='%s')\n"), kernelMountedDevicesMax, format, devicePath, dirPath);
		return false;
	}

	// Open device file
	KernelFsFd deviceFd=kernelFsFileOpen(devicePath);
	if (deviceFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not mount - could not open device file (format=%u, devicePath='%s', dirPath='%s')\n"), format, devicePath, dirPath);
		return false;
	}

	// Add to array of mounted devices (so we can unmount later)
	kernelMountedDevices[kernelMountedDevicesNext].format=format;
	kernelMountedDevices[kernelMountedDevicesNext].fd=deviceFd;
	++kernelMountedDevicesNext;

	// Format-type-specific logic
	switch(format) {
		case KernelMountFormatMiniFs: {
			// Add virtual block device to virtual file system
			KernelFsFileOffset size=kernelFsFileGetLen(devicePath);
			if (!kernelFsAddBlockDeviceFile(kstrC(dirPath), &kernelMountFsFunctor, (void *)(uintptr_t)(deviceFd), KernelFsBlockDeviceFormatCustomMiniFs, size, true)) {
				kernelLog(LogTypeWarning, kstrP("could not mount - could not add virtual block device file (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
				goto error;
			}
		} break;
		case KernelMountFormatFlatFile: {
			// Add virtual block device to virtual file system
			KernelFsFileOffset size=kernelFsFileGetLen(devicePath);
			if (!kernelFsAddBlockDeviceFile(kstrC(dirPath), &kernelMountFsFunctor, (void *)(uintptr_t)(deviceFd), KernelFsBlockDeviceFormatFlatFile, size, true)) {
				kernelLog(LogTypeWarning, kstrP("could not mount - could not add virtual block device file (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
				goto error;
			}
		} break;
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// Read partition table
			unsigned partitionIndex=format-KernelMountFormatPartition1;

			PTableEntry entry;
			if (!pTableParseEntryFd(deviceFd, partitionIndex, &entry)) {
				kernelLog(LogTypeWarning, kstrP("could not mount - could not parse partition table entry %u (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), partitionIndex+1, format, devicePath, dirPath, deviceFd);
				goto error;
			}

			KernelFsFileOffset size=(entry.numSectors<8388607lu ? entry.numSectors : 8388607lu)*512;

			// Add virtual block device to virtual file system
			if (!kernelFsAddBlockDeviceFile(kstrC(dirPath), &kernelMountFsFunctor, (void *)(uintptr_t)(deviceFd), KernelFsBlockDeviceFormatFlatFile, size, true)) {
				kernelLog(LogTypeWarning, kstrP("could not mount - could not add virtual block device file (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
				goto error;
			}
		} break;
	}

	// Success
	kernelLog(LogTypeInfo, kstrP("mount successful (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
	return true;

	// Error
	error:
	--kernelMountedDevicesNext;
	kernelFsFileClose(deviceFd);
	return false;
}

void kernelUnmount(const char *devicePath) {
	// Check path is valid
	if (!kernelFsPathIsValid(devicePath)) {
		kernelLog(LogTypeWarning, kstrP("could not unmount - bad path (devicePath='%s')\n"), devicePath);
		return;
	}

	// Look through device fd array for a one representing the given path
	for(uint8_t i=0; i<kernelMountedDevicesNext; ++i) {
		KernelFsFd fd=kernelMountedDevices[i].fd;
		if (kstrStrcmp(devicePath, kernelFsGetFilePath(fd))==0) {
			// Match found

			// Delete/unmount virtual block device file
			// TODO: where do we get this from? also kernelFs will probably reject the request anyway if the 'directory' is non-empty
			// kernelFsFileDelete(const char *path); // TODO: Check return

			// Close device file
			kernelFsFileClose(fd);

			// Remove fd from our array
			kernelMountedDevices[i]=kernelMountedDevices[--kernelMountedDevicesNext];

			// Success
			kernelLog(LogTypeInfo, kstrP("unmounted (devicePath='%s')\n"), devicePath);
			return;
		}
	}

	kernelLog(LogTypeWarning, kstrP("could not unmount - no such device mounted (devicePath='%s')\n"), devicePath);
}

uint32_t kernelMountFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return kernelMountFlushFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
			return kernelMountReadFunctor(addr, data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
			return kernelMountWriteFunctor(addr, data, len, userData);
		break;
	}

	assert(false);
	return 0;
}

bool kernelMountFlushFunctor(void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;

	KStr pathK=kernelFsGetFilePath(deviceFd);
	if (kstrIsNull(pathK))
		return false; // shouldn't really happen but for safety

	char path[KernelFsPathMax];
	kstrStrcpy(path, pathK);
	return kernelFsFileFlush(path);
}

KernelFsFileOffset kernelMountReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;
	const KernelMountDevice *device=kernelMountGetDeviceFromFd(deviceFd);
	assert(device!=NULL);

	switch(device->format) {
		case KernelMountFormatMiniFs:
		case KernelMountFormatFlatFile:
			// Simply read from device file directly
			return kernelFsFileReadOffset(deviceFd, addr, data, len);
		break;
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// Read partition table
			unsigned partitionIndex=device->format-KernelMountFormatPartition1;

			PTableEntry entry;
			if (!pTableParseEntryFd(deviceFd, partitionIndex, &entry))
				return 0;

			KernelFsFileOffset offset=(entry.startSector<8388607lu ? entry.startSector : 8388607lu)*((uint32_t)512);

			// Read from device file
			return kernelFsFileReadOffset(deviceFd, addr+offset, data, len);
		} break;
	}

	assert(false);
	return 0;
}

KernelFsFileOffset kernelMountWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;
	const KernelMountDevice *device=kernelMountGetDeviceFromFd(deviceFd);
	assert(device!=NULL);

	switch(device->format) {
		case KernelMountFormatMiniFs:
		case KernelMountFormatFlatFile:
			// Simply write to device file directly
			return kernelFsFileWriteOffset(deviceFd, addr, data, len);
		break;
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// Read partition table
			unsigned partitionIndex=device->format-KernelMountFormatPartition1;

			PTableEntry entry;
			if (!pTableParseEntryFd(deviceFd, partitionIndex, &entry))
				return 0;

			KernelFsFileOffset offset=(entry.startSector<8388607lu ? entry.startSector : 8388607lu)*((uint32_t)512);

			// Write to device file
			return kernelFsFileWriteOffset(deviceFd, addr+offset, data, len);
		} break;
	}

	assert(false);
	return 0;
}

const KernelMountDevice *kernelMountGetDeviceFromFd(KernelFsFd fd) {
	for(uint8_t i=0; i<kernelMountedDevicesNext; ++i)
		if (fd==kernelMountedDevices[i].fd)
			return &kernelMountedDevices[i];
	return NULL;
}
