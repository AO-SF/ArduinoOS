#include <assert.h>
#include <string.h>

#include "kernelmount.h"
#include "log.h"

#define kernelMountedDeviceFdsMax 32
uint8_t kernelMountedDeviceFdsNext=0;
KernelFsFd kernelMountedDeviceFds[kernelMountedDeviceFdsMax];

int kernelMountReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelMountWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);

bool kernelMount(KernelFsBlockDeviceFormat format, const char *devicePath, const char *dirPath) {
	// Check paths are valid
	if (!kernelFsPathIsValid(devicePath) || !kernelFsPathIsValid(dirPath)) {
		kernelLog(LogTypeWarning, "could not mount - bad path(s) (format=%u, devicePath='%s', dirPath='%s')\n", format, devicePath, dirPath);
		return false;
	}

	// Check we have space to mount another device
	if (kernelMountedDeviceFdsNext>=kernelMountedDeviceFdsMax) {
		kernelLog(LogTypeWarning, "could not mount - already have maximum number of devices mounted (%u) (format=%u, devicePath='%s', dirPath='%s')\n", kernelMountedDeviceFdsMax, format, devicePath, dirPath);
		return false;
	}

	// Open device file
	KernelFsFd deviceFd=kernelFsFileOpen(devicePath);
	if (deviceFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, "could not mount - could not open device file (format=%u, devicePath='%s', dirPath='%s')\n", format, devicePath, dirPath);
		return false;
	}

	// Add virtual block device to virtual file system
	if (!kernelFsAddBlockDeviceFile(dirPath, format, &kernelMountReadFunctor, &kernelMountWriteFunctor, (void *)(uintptr_t)(deviceFd))) {
		kernelLog(LogTypeWarning, "could not mount - could not add virtual block device file (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n", format, devicePath, dirPath, deviceFd);
		return false;
	}

	// Add to array of mounted devices (so we can unmount later)
	kernelMountedDeviceFds[kernelMountedDeviceFdsNext]=deviceFd;
	++kernelMountedDeviceFdsNext;

	// Success
	kernelLog(LogTypeInfo, "mount successful (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n", format, devicePath, dirPath, deviceFd);
	return true;
}

void kernelUnmount(const char *devicePath) {
	// Check path is valid
	if (!kernelFsPathIsValid(devicePath)) {
		kernelLog(LogTypeWarning, "could not unmount - bad path (devicePath='%s')\n", devicePath);
		return;
	}

	// Look through device fd array for a one representing the given path
	for(uint8_t i=0; i<kernelMountedDeviceFdsNext; ++i) {
		KernelFsFd fd=kernelMountedDeviceFds[i];
		if (strcmp(devicePath, kernelFsGetFilePath(fd))==0) {
			// Match found

			// Delete/unmount virtual block device file
			// TODO: where do we get this from? also kernelFs will probably reject the request anyway if the 'directory' is non-empty
			// kernelFsFileDelete(const char *path); // TODO: Check return

			// Close device file
			kernelFsFileClose(fd);

			// Remove fd from our array
			kernelMountedDeviceFds[i]=kernelMountedDeviceFds[--kernelMountedDeviceFdsNext];

			// Success
			kernelLog(LogTypeInfo, "unmounted (devicePath='%s')\n", devicePath);
			return;
		}
	}

	kernelLog(LogTypeWarning, "could not unmount - no such device mounted (devicePath='%s')\n", devicePath);
}

int kernelMountReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);
	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;

	// Simply read from device file directly
	uint8_t value;
	if (kernelFsFileReadOffset(deviceFd, addr, &value, 1, false)!=1)
		return -1;
	return value;
}

bool kernelMountWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);
	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)addr;

	// Simply write to device file directly
	return (kernelFsFileWriteOffset(deviceFd, addr, &value, 1)==1);
}
