#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernelmount.h"
#include "log.h"
#include "minifs.h"
#include "ptable.h"

typedef struct {
	uint8_t format;
	KernelFsFd fd;
} KernelMountDevice;

#define kernelMountedDevicesMax 32
uint8_t kernelMountedDevicesNext=0;
KernelMountDevice kernelMountedDevices[kernelMountedDevicesMax];

uint32_t kernelMountFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
bool kernelMountCommonFlushFunctor(void *userData);
KernelFsFileOffset kernelMountBlockReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelMountBlockWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelMountCharacterReadFunctor(void *userData);
bool kernelMountCharacterCanReadFunctor(void *userData);
KernelFsFileOffset kernelMountCharacterWriteFunctor(void *userData, const uint8_t *data, KernelFsFileOffset len);
bool kernelMountCharacterCanWriteFunctor(void *userData);

const KernelMountDevice *kernelMountGetDeviceFromFd(KernelFsFd fd);

unsigned kernelMountCircBufGetOffsetSize(KernelFsFd deviceFd); // returns size in bytes used by the head/tail offsets individually, either 1, 2 or 4
KernelFsFileOffset kernelMountCircBufGetHeadOffset(KernelFsFd deviceFd); // offsets are in range [0,kernelMountCircBufGetBufferSize-2], note extra -1 is to be able to distinguish between empty and full
KernelFsFileOffset kernelMountCircBufGetTailOffset(KernelFsFd deviceFd); // these return KernelFsFileOffsetMax on failure to read from backing file
bool kernelMountCircBufSetHeadOffset(KernelFsFd deviceFd, KernelFsFileOffset headOffset);
bool kernelMountCircBufSetTailOffset(KernelFsFd deviceFd, KernelFsFileOffset tailOffset);
KernelFsFileOffset kernelMountCircBufGetBufferSize(KernelFsFd deviceFd); // total size minus space used by head and tail offsets
KernelFsFileOffset kernelMountCircBufGetBackingSize(KernelFsFd deviceFd); // total size available to circ buf implementation
KernelFsFileOffset kernelMountCircBufIncOffset(KernelFsFd deviceFd, KernelFsFileOffset offset);

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
	KernelFsFd deviceFd=kernelFsFileOpen(devicePath, KernelFsFdModeRW);
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
		case KernelMountFormatCircBuf: {
			// Check if the backing file is too small to use
			if (kernelFsFileGetLen(devicePath)<4) {
				kernelLog(LogTypeWarning, kstrP("could not mount - backing file too small (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
				goto error;
			}

			// Add virtual character device to virtual file system
			if (!kernelFsAddCharacterDeviceFile(kstrC(dirPath), &kernelMountFsFunctor, (void *)(uintptr_t)(deviceFd), true, true)) {
				kernelLog(LogTypeWarning, kstrP("could not mount - could not add virtual character device file (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
				goto error;
			}

			// Write head and tail offsets to indicate en empty state
			if (!kernelMountCircBufSetHeadOffset(deviceFd, 0) || !kernelMountCircBufSetTailOffset(deviceFd, 0)) {
				// TODO: remove character device file added above (as we do in kernelUnmount)
				kernelLog(LogTypeWarning, kstrP("could not mount - could not write head/tail offsets (format=%u, devicePath='%s', dirPath='%s', device fd=%u)\n"), format, devicePath, dirPath, deviceFd);
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

void kernelUnmount(const char *dirPath) {
	// Check path is valid
	if (!kernelFsPathIsValid(dirPath)) {
		kernelLog(LogTypeWarning, kstrP("could not unmount - bad path (dirPath='%s')\n"), dirPath);
		return;
	}

	// Grab deviceFd stored in dirPath device userdata
	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)kernelFsDeviceFileGetUserData(dirPath);

	// Find entry in our table for this mount
	uint8_t i;
	for(i=0; i<kernelMountedDevicesNext; ++i)
		if (deviceFd==kernelMountedDevices[i].fd)
			break;

	if (i>=kernelMountedDevicesNext) {
		kernelLog(LogTypeWarning, kstrP("could not unmount - no such device mounted (dirPath='%s', deviceFd=%u)\n"), dirPath, deviceFd);
		return;
	}

	// Remove dirPath mount
	kernelFsRemoveDeviceFile(dirPath);

	// Close device file
	kernelFsFileClose(deviceFd);

	// Remove entry from our array
	kernelMountedDevices[i]=kernelMountedDevices[--kernelMountedDevicesNext];

	// Success
	kernelLog(LogTypeInfo, kstrP("unmounted (dirPath='%s', slot=%u)\n"), dirPath, i);
}

bool kernelRemount(KernelMountFormat newFormat, const char *newDevicePath, const char *dirPath) {
	char pathBuffer[KernelFsPathMax];
	uint8_t copyBuffer[kernelRemountCopyBufferSize];
	return kernelRemountWithBuffers(newFormat, newDevicePath, dirPath, pathBuffer, copyBuffer);
}

bool kernelRemountWithBuffers(KernelMountFormat newFormat, const char *newDevicePath, const char *dirPath, char *pathBuffer, uint8_t *copyBuffer) {
	// Check dir path is valid
	if (!kernelFsPathIsValid(dirPath)) {
		kernelLog(LogTypeWarning, kstrP("could not remount - bad dir path (newDevicePath='%s', dirPath='%s')\n"), newDevicePath, dirPath);
		return false;
	}

	// Grab existing device fd stored in dirPath device userdata
	KernelFsFd oldDeviceFd=(KernelFsFd)(uintptr_t)kernelFsDeviceFileGetUserData(dirPath);

	// Find entry in our table for this mount
	uint8_t i;
	for(i=0; i<kernelMountedDevicesNext; ++i)
		if (oldDeviceFd==kernelMountedDevices[i].fd)
			break;

	if (i>=kernelMountedDevicesNext) {
		kernelLog(LogTypeWarning, kstrP("could not remount - no such device mounted (newDevicePath='%s', dirPath='%s', oldDeviceFd=%u)\n"), newDevicePath, dirPath, oldDeviceFd);
		return false;
	}

	// Verify new format is compatible
	bool isDir=KernelMountFormatIsDir(newFormat);
	KernelMountFormat oldFormat=kernelMountedDevices[i].format;
	if (isDir!=KernelMountFormatIsDir(oldFormat)) {
		kernelLog(LogTypeWarning, kstrP("could not remount - mismatched formats %u and %u (newDevicePath='%s', dirPath='%s', oldDeviceFd=%u)\n"), newFormat, oldFormat, newDevicePath, dirPath, oldDeviceFd);
		return false;
	}

	// Open new device file
	KernelFsFd newDeviceFd=kernelFsFileOpen(newDevicePath, KernelFsFdModeRW);
	if (newDeviceFd==KernelFsFdInvalid) {
		kernelLog(LogTypeWarning, kstrP("could not remount - could not open new device file (newFormat=%u, newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), newFormat, newDevicePath, oldDeviceFd, dirPath);
		return false;
	}

	KernelFsFileOffset newDeviceLen=kernelFsFileGetLen(newDevicePath);

	// Format new 'volume' and copy over files/contents from old one
	switch(newFormat) {
		case KernelMountFormatMiniFs:
			// Format the new backing file to be a MiniFs volume
			if (!miniFsFormat(&kernelFsFdMiniFsWriteWrapper, (void *)(uintptr_t)newDeviceFd, newDeviceLen)) {
				kernelLog(LogTypeWarning, kstrP("could not remount - could not format MiniFs volume (len=%u, newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), newDeviceLen, newDevicePath, oldDeviceFd, dirPath);
				kernelFsFileClose(newDeviceFd);
				return false;
			}

			// Copy files
			MiniFs oldMiniFs, newMiniFs;
			if (!miniFsMountFast(&oldMiniFs, &kernelFsFdMiniFsReadWrapper, &kernelFsFdMiniFsWriteWrapper, (void *)(uintptr_t)oldDeviceFd)) {
				kernelLog(LogTypeWarning, kstrP("could not remount - could not mount old MiniFs volume (newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), newDevicePath, oldDeviceFd, dirPath);
				kernelFsFileClose(newDeviceFd);
				return false;
			}
			if (!miniFsMountFast(&newMiniFs, &kernelFsFdMiniFsReadWrapper, &kernelFsFdMiniFsWriteWrapper, (void *)(uintptr_t)newDeviceFd)) {
				kernelLog(LogTypeWarning, kstrP("could not remount - could not mount new MiniFs volume (newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), newDevicePath, oldDeviceFd, dirPath);
				kernelFsFileClose(newDeviceFd);
				return false;
			}

			uint8_t childCount=miniFsGetChildCount(&oldMiniFs);
			for(uint8_t child=0; child<childCount; ++child) {
				if (miniFsGetChildN(&oldMiniFs, child, pathBuffer)) {
					// Create file in new volume
					uint16_t childLen=miniFsFileGetLen(&oldMiniFs, pathBuffer);
					if (!miniFsFileCreate(&newMiniFs, pathBuffer, childLen)) {
						kernelLog(LogTypeWarning, kstrP("could not remount - could not create file '%s' in new MiniFs volume (newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), pathBuffer, newDevicePath, oldDeviceFd, dirPath);
						kernelFsFileClose(newDeviceFd);
						return false;
					}

					// Copy current file to new volume
					uint16_t offset;
					for(offset=0; offset<childLen; offset+=kernelRemountCopyBufferSize) {
						uint16_t chunkSize=childLen-offset;
						if (chunkSize>kernelRemountCopyBufferSize)
							chunkSize=kernelRemountCopyBufferSize;

						if (miniFsFileRead(&oldMiniFs, pathBuffer, offset, copyBuffer, chunkSize)!=chunkSize) {
							kernelLog(LogTypeWarning, kstrP("could not remount - could not read file '%s' from old MiniFs volume (newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), pathBuffer, newDevicePath, oldDeviceFd, dirPath);
							kernelFsFileClose(newDeviceFd);
							return false;
						}
						if (miniFsFileWrite(&newMiniFs, pathBuffer, offset, copyBuffer, chunkSize)!=chunkSize) {
							kernelLog(LogTypeWarning, kstrP("could not remount - could not write file '%s' to new MiniFs volume (newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), pathBuffer, newDevicePath, oldDeviceFd, dirPath);
							kernelFsFileClose(newDeviceFd);
							return false;
						}

					}
				}
			}

			miniFsUnmount(&oldMiniFs);
			miniFsUnmount(&newMiniFs);
		break;
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4:
		case KernelMountFormatCircBuf:
			// TODO: these cases
			kernelFsFileClose(newDeviceFd);
			kernelLog(LogTypeWarning, kstrP("could not remount - unhandled new format %u (newDevicePath='%s', oldDeviceFd=%u, dirPath='%s')\n"), newFormat, newDevicePath, oldDeviceFd, dirPath);
			return false;
		break;
	}

	// Update our array to indicate the changes
	// Note: we have to do this now so that we can use kernelMountFsFunctor when adding the new device file
	kernelMountedDevices[i].format=newFormat;
	kernelMountedDevices[i].fd=newDeviceFd;

	// Update device file at dirPath
	switch(newFormat) {
		case KernelMountFormatMiniFs:
			// Add virtual block device to virtual file system
			// Note: we reuse the pathBuffer here but for a different purpose than above
			if (!kernelFsUpdateBlockDeviceFileWithBuffer(kstrC(dirPath), &kernelMountFsFunctor, (void *)(uintptr_t)(newDeviceFd), KernelFsBlockDeviceFormatCustomMiniFs, newDeviceLen, true, pathBuffer)) {
				kernelLog(LogTypeWarning, kstrP("could not remount - could not add new virtual block device file (newFormat=%u, newDevicePath='%s', dirPath='%s', newDeviceFd=%u)\n"), newFormat, newDevicePath, dirPath, newDeviceFd);
				goto postcloseerror;
			}
		break;
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4:
		case KernelMountFormatCircBuf:
			// TODO: these cases
			assert(false); // should have triggered early return in above switch statement
			goto postcloseerror;
		break;
	}

	// Close old device file
	kernelFsFileClose(oldDeviceFd);

	// Success
	kernelLog(LogTypeInfo, kstrP("remounted (newFormat=%u, newDevicePath='%s', dirPath='%s', slot=%u)\n"), newFormat, newDevicePath, dirPath, i);

	return true;

	postcloseerror:

	// Close newly opened device
	kernelFsFileClose(newDeviceFd);

	// Reset kernelMountedDevices array
	kernelMountedDevices[i].format=oldFormat;
	kernelMountedDevices[i].fd=oldDeviceFd;

	return false;
}

bool KernelMountFormatIsFile(KernelMountFormat format) {
	switch(format) {
		case KernelMountFormatMiniFs:
			return false;
		break;
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4:
		case KernelMountFormatCircBuf:
			return true;
		break;
	}

	return false;
}

bool KernelMountFormatIsDir(KernelMountFormat format) {
	switch(format) {
		case KernelMountFormatMiniFs:
			return true;
		break;
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4:
		case KernelMountFormatCircBuf:
			return false;
		break;
	}

	return false;
}

uint32_t kernelMountFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return kernelMountCommonFlushFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
			return kernelMountCharacterReadFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
			return kernelMountCharacterCanReadFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
			return kernelMountCharacterWriteFunctor(userData, data, len);
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
			return kernelMountCharacterCanWriteFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
			return kernelMountBlockReadFunctor(addr, data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
			return kernelMountBlockWriteFunctor(addr, data, len, userData);
		break;
	}

	assert(false);
	return 0;
}

bool kernelMountCommonFlushFunctor(void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;

	KStr pathK=kernelFsGetFilePath(deviceFd);
	if (kstrIsNull(pathK))
		return false; // shouldn't really happen but for safety

	char path[KernelFsPathMax];
	kstrStrcpy(path, pathK);
	return kernelFsFileFlush(path);
}

KernelFsFileOffset kernelMountBlockReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
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
		case KernelMountFormatCircBuf: {
			// These should be character devices, not block
			assert(false);
			return 0;
		} break;
	}

	assert(false);
	return 0;
}

KernelFsFileOffset kernelMountBlockWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
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
		case KernelMountFormatCircBuf: {
			// These should be character devices, not block
			assert(false);
			return 0;
		} break;
	}

	assert(false);
	return 0;
}

int16_t kernelMountCharacterReadFunctor(void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;
	const KernelMountDevice *device=kernelMountGetDeviceFromFd(deviceFd);
	assert(device!=NULL);

	switch(device->format) {
		case KernelMountFormatMiniFs:
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// These are not character devices
			assert(false);
			return -1;
		} break;
		case KernelMountFormatCircBuf: {
			// Get offset info
			KernelFsFileOffset offsetSize=kernelMountCircBufGetOffsetSize(deviceFd);
			KernelFsFileOffset headOffset=kernelMountCircBufGetHeadOffset(deviceFd);
			KernelFsFileOffset tailOffset=kernelMountCircBufGetTailOffset(deviceFd);

			// Empty?
			if (headOffset==tailOffset)
				return -1;

			// Read byte at current head
			uint8_t result;
			if (!kernelFsFileReadByte(deviceFd, 2*offsetSize+headOffset, &result))
				return -1;

			// Advance head to consume byte and write it back
			if (!kernelMountCircBufSetHeadOffset(deviceFd, kernelMountCircBufIncOffset(deviceFd, headOffset)))
				return -1;

			return result;
		} break;
	}

	assert(false);
	return -1;
}

bool kernelMountCharacterCanReadFunctor(void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;
	const KernelMountDevice *device=kernelMountGetDeviceFromFd(deviceFd);
	assert(device!=NULL);

	switch(device->format) {
		case KernelMountFormatMiniFs:
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// These are not character devices
			assert(false);
			return false;
		} break;
		case KernelMountFormatCircBuf: {
			// Grab head and tail offsets
			KernelFsFileOffset headOffset=kernelMountCircBufGetHeadOffset(deviceFd);
			KernelFsFileOffset tailOffset=kernelMountCircBufGetTailOffset(deviceFd);
			if (headOffset==KernelFsFileOffsetMax || tailOffset==KernelFsFileOffsetMax)
				return false;

			// If offsets are not equal then the buffer is not empty and so there is something to read
			return (headOffset!=tailOffset);
		} break;
	}

	assert(false);
	return false;
}

KernelFsFileOffset kernelMountCharacterWriteFunctor(void *userData, const uint8_t *data, KernelFsFileOffset len) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;
	const KernelMountDevice *device=kernelMountGetDeviceFromFd(deviceFd);
	assert(device!=NULL);

	switch(device->format) {
		case KernelMountFormatMiniFs:
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// These are not character devices
			assert(false);
			return 0;
		} break;
		case KernelMountFormatCircBuf: {
			// Get offset info
			KernelFsFileOffset offsetSize=kernelMountCircBufGetOffsetSize(deviceFd);
			KernelFsFileOffset headOffset=kernelMountCircBufGetHeadOffset(deviceFd);
			KernelFsFileOffset tailOffset=kernelMountCircBufGetTailOffset(deviceFd);

			// Writing loop
			KernelFsFileOffset written;
			for(written=0; written<len; ++written) {
				// Full?
				KernelFsFileOffset newTailOffset=kernelMountCircBufIncOffset(deviceFd, tailOffset);
				if (newTailOffset==headOffset)
					break;

				// Write one byte at current tail
				if (!kernelFsFileWriteByte(deviceFd, 2*offsetSize+tailOffset, data[written]))
					return 0;

				// Update tail offset to virtually push the byte
				tailOffset=newTailOffset;
			}

			// Write back tail offset as potentially changed
			if (!kernelMountCircBufSetTailOffset(deviceFd, tailOffset))
				return 0;

			return written;
		} break;
	}

	assert(false);
	return 0;
}

bool kernelMountCharacterCanWriteFunctor(void *userData) {
	assert(((uintptr_t)userData)<KernelFsFdMax);

	KernelFsFd deviceFd=(KernelFsFd)(uintptr_t)userData;
	const KernelMountDevice *device=kernelMountGetDeviceFromFd(deviceFd);
	assert(device!=NULL);

	switch(device->format) {
		case KernelMountFormatMiniFs:
		case KernelMountFormatFlatFile:
		case KernelMountFormatPartition1:
		case KernelMountFormatPartition2:
		case KernelMountFormatPartition3:
		case KernelMountFormatPartition4: {
			// These are not character devices
			assert(false);
			return false;
		} break;
		case KernelMountFormatCircBuf: {
			// Grab head and tail offsets
			KernelFsFileOffset headOffset=kernelMountCircBufGetHeadOffset(deviceFd);
			KernelFsFileOffset tailOffset=kernelMountCircBufGetTailOffset(deviceFd);
			if (headOffset==KernelFsFileOffsetMax || tailOffset==KernelFsFileOffsetMax)
				return false;

			// If tail offset is about to meet head offset, then buffer is full. Otherwise there is space and a write would proceed.
			KernelFsFileOffset newTailOffset=kernelMountCircBufIncOffset(deviceFd, tailOffset);
			return (newTailOffset!=headOffset);
		} break;
	}

	assert(false);
	return false;
}

const KernelMountDevice *kernelMountGetDeviceFromFd(KernelFsFd fd) {
	for(uint8_t i=0; i<kernelMountedDevicesNext; ++i)
		if (fd==kernelMountedDevices[i].fd)
			return &kernelMountedDevices[i];
	return NULL;
}

unsigned kernelMountCircBufGetOffsetSize(KernelFsFd deviceFd) {
	KernelFsFileOffset backingFileSize=kernelMountCircBufGetBackingSize(deviceFd);
	if (backingFileSize<(((KernelFsFileOffset)1)<<8))
		return 1; // 256 bytes max
	else if (backingFileSize<(((KernelFsFileOffset)1)<<16))
		return 2; // 64kb max
	else
		return 4; // 4gb max
}

KernelFsFileOffset kernelMountCircBufGetHeadOffset(KernelFsFd deviceFd) {
	unsigned offsetSize=kernelMountCircBufGetOffsetSize(deviceFd);
	switch(offsetSize) {
		case 1: {
			uint8_t headOffset;
			if (kernelFsFileReadByte(deviceFd, 0, &headOffset))
				return headOffset;
		} break;
		case 2: {
			uint16_t headOffset;
			if (kernelFsFileReadWord(deviceFd, 0, &headOffset))
				return headOffset;
		} break;
		case 4: {
			uint32_t headOffset;
			if (kernelFsFileReadDoubleWord(deviceFd, 0, &headOffset))
				return headOffset;
		} break;
	}
	return KernelFsFileOffsetMax;
}

KernelFsFileOffset kernelMountCircBufGetTailOffset(KernelFsFd deviceFd) {
	unsigned offsetSize=kernelMountCircBufGetOffsetSize(deviceFd);
	switch(offsetSize) {
		case 1: {
			uint8_t headOffset;
			if (kernelFsFileReadByte(deviceFd, offsetSize, &headOffset))
				return headOffset;
		} break;
		case 2: {
			uint16_t headOffset;
			if (kernelFsFileReadWord(deviceFd, offsetSize, &headOffset))
				return headOffset;
		} break;
		case 4: {
			uint32_t headOffset;
			if (kernelFsFileReadDoubleWord(deviceFd, offsetSize, &headOffset))
				return headOffset;
		} break;
	}
	return KernelFsFileOffsetMax;
}

bool kernelMountCircBufSetHeadOffset(KernelFsFd deviceFd, KernelFsFileOffset headOffset) {
	unsigned offsetSize=kernelMountCircBufGetOffsetSize(deviceFd);
	switch(offsetSize) {
		case 1:
			return kernelFsFileWriteByte(deviceFd, 0, headOffset);
		break;
		case 2:
			return kernelFsFileWriteWord(deviceFd, 0, headOffset);
		break;
		case 4:
			return kernelFsFileWriteDoubleWord(deviceFd, 0, headOffset);
		break;
	}
	return false;
}

bool kernelMountCircBufSetTailOffset(KernelFsFd deviceFd, KernelFsFileOffset tailOffset) {
	unsigned offsetSize=kernelMountCircBufGetOffsetSize(deviceFd);
	switch(offsetSize) {
		case 1:
			return kernelFsFileWriteByte(deviceFd, offsetSize, tailOffset);
		break;
		case 2:
			return kernelFsFileWriteWord(deviceFd, offsetSize, tailOffset);
		break;
		case 4:
			return kernelFsFileWriteDoubleWord(deviceFd, offsetSize, tailOffset);
		break;
	}
	return false;
}

KernelFsFileOffset kernelMountCircBufGetBufferSize(KernelFsFd deviceFd) {
	KernelFsFileOffset backingFileSize=kernelMountCircBufGetBackingSize(deviceFd);
	if (backingFileSize<(((KernelFsFileOffset)1)<<8))
		return backingFileSize-2*1; // 2 bytes used for offsets
	else if (backingFileSize<(((KernelFsFileOffset)1)<<16))
		return backingFileSize-2*2; // 4 bytes used for offsets
	else
		return backingFileSize-2*4; // 8 bytes used for offsets
}

KernelFsFileOffset kernelMountCircBufGetBackingSize(KernelFsFd deviceFd) {
	KStr backingFilePath=kernelFsGetFilePath(deviceFd);
	char backingFilePathC[KernelFsPathMax];
	kstrStrcpy(backingFilePathC, backingFilePath);
	return kernelFsFileGetLen(backingFilePathC);
}

KernelFsFileOffset kernelMountCircBufIncOffset(KernelFsFd deviceFd, KernelFsFileOffset offset) {
	KernelFsFileOffset bufferSize=kernelMountCircBufGetBufferSize(deviceFd);
	if (offset<bufferSize-1)
		return offset+1;
	else
		return 0;
}
