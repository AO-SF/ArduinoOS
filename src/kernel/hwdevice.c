#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "kernelfs.h"
#include "log.h"
#include "pins.h"
#include "sd.h"
#include "hwdevice.h"
#include "util.h"

typedef struct {
	uint8_t powerPin;
	uint8_t slaveSelectPin;
} HwDevicePinPair;

const HwDevicePinPair hwDevicePinPairs[HwDeviceIdMax]={
	{.powerPin=PinD46, .slaveSelectPin=PinD47},
	{.powerPin=PinD48, .slaveSelectPin=PinD49},
};

typedef struct {
	KStr mountPoint;
	SdCard sdCard; // type set to SdTypeBadCard when none mounted
	uint8_t *cache; // malloc'd, SdBlockSize in size
	uint32_t cacheIsValid:1; // if false, cacheBlock field is undefined as is the data in the cache array
	uint32_t cacheIsDirty:1; // if cache is valid, then this represents whether the cache array has been modified since reading
	uint32_t cacheBlock:30; // Note: using only 30 bits is safe as some bits of addresses are 'used up' by the fixed size 512 byte blocks, so not all 32 bits are needed (only 32-9=23 strictly needed)
} HwDeviceSdCardReaderData;

typedef struct {
	HwDeviceType type;
	union {
		HwDeviceSdCardReaderData sdCardReader;
	} d;
} HwDevice;

HwDevice hwDevices[HwDeviceIdMax];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

KernelFsFileOffset hwDeviceSdCardReaderReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset hwDeviceSdCardReaderWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void hwDeviceInit(void) {
	// Set all pins as output, with power pins low (no power) and slave select pins high (disabled).
	for(unsigned i=0; i<HwDeviceIdMax; ++i) {
		pinSetMode(hwDevicePinPairs[i].powerPin, PinModeOutput);
		pinWrite(hwDevicePinPairs[i].powerPin, false);

		pinSetMode(hwDevicePinPairs[i].slaveSelectPin, PinModeOutput);
		pinWrite(hwDevicePinPairs[i].slaveSelectPin, true);
	}

	// Clear device table
	for(unsigned i=0; i<HwDeviceIdMax; ++i) {
		hwDevices[i].type=HwDeviceTypeUnused;
	}
}

bool hwDeviceRegister(HwDeviceId id, HwDeviceType type) {
	// Bad id or type?
	if (id>=HwDeviceIdMax || type==HwDeviceTypeUnused)
		return false;

	// Slot already in use?
	if (hwDevices[id].type!=HwDeviceTypeUnused)
		return false;

	// Write to log
	kernelLog(LogTypeInfo, kstrP("registered SPI device id=%u type=%u\n"), id, type);

	// Type-specific logic
	switch(type) {
		case HwDeviceTypeUnused:
		break;
		case HwDeviceTypeRaw:
		break;
		case HwDeviceTypeSdCardReader:
			hwDevices[id].d.sdCardReader.cache=malloc(SdBlockSize);
			if (hwDevices[id].d.sdCardReader.cache==NULL)
				return false;
			hwDevices[id].d.sdCardReader.sdCard.type=SdTypeBadCard;
		break;
	}

	// Set type to mark slot as used
	hwDevices[id].type=type;

	return true;
}

void hwDeviceDeregister(HwDeviceId id) {
	// Bad if?
	if (id>=HwDeviceIdMax)
		return;

	// Slot not even in use?
	if (hwDevices[id].type==HwDeviceTypeUnused)
		return;

	// Type-specific logic
	switch(hwDevices[id].type) {
		case HwDeviceTypeUnused:
		break;
		case HwDeviceTypeRaw:
		break;
		case HwDeviceTypeSdCardReader:
			// We may have to unmount an SD card
			hwDeviceSdCardReaderUnmount(id);

			// Free memory
			free(hwDevices[id].d.sdCardReader.cache);
		break;
	}

	// Force pins back to default to be safe
	pinWrite(hwDevicePinPairs[id].powerPin, false);
	pinWrite(hwDevicePinPairs[id].slaveSelectPin, true);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("deregistered SPI device id=%u type=%u\n"), id, hwDevices[id].type);

	// Clear type to mark slot as unused
	hwDevices[id].type=HwDeviceTypeUnused;
}

HwDeviceType hwDeviceGetType(HwDeviceId id) {
	if (id>=HwDeviceIdMax)
		return HwDeviceTypeUnused;

	return hwDevices[id].type;
}

HwDeviceId hwDeviceGetDeviceForPin(uint8_t pinNum) {
	for(unsigned i=0; i<HwDeviceIdMax; ++i)
		if (pinNum==hwDevicePinPairs[i].powerPin || pinNum==hwDevicePinPairs[i].slaveSelectPin)
			return i;
	return HwDeviceIdMax;
}

bool hwDeviceSdCardReaderMount(HwDeviceId id, const char *mountPoint) {
	// Bad id?
	if (id>=HwDeviceIdMax) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: bad id (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Device slot not used for an SD card reader?
	if (hwDeviceGetType(id)!=HwDeviceTypeSdCardReader) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: bad device type (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Already have a card mounted using this device?
	if (hwDevices[id].d.sdCardReader.sdCard.type!=SdTypeBadCard) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: card already mounted (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Attempt to power on and initialise SD card
	SdInitResult sdInitRes=sdInit(&hwDevices[id].d.sdCardReader.sdCard, hwDevicePinPairs[id].powerPin, hwDevicePinPairs[id].slaveSelectPin);
	if (sdInitRes!=SdInitResultOk) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: sd init failed with %u (id=%u, mountPoint='%s')\n"), sdInitRes, id, mountPoint);
		return false;
	}

	// Mark cache block as undefined (before we even register read/write functors to be safe).
	hwDevices[id].d.sdCardReader.cacheIsValid=false;

	// Add block device at given point mount
	uint32_t maxBlockCount=(((uint32_t)1u)<<(32-SdBlockSizeBits)); // we are limited by 32 bit addresses, regardless of how large blocks are

	KernelFsFileOffset size=hwDevices[id].d.sdCardReader.sdCard.blockCount;
	if (size>=maxBlockCount) {
		kernelLog(LogTypeWarning, kstrP("SPI device SD card reader mount: block count too large, reducing from %"PRIu32" to %"PRIu32" (id=%u, mountPoint='%s')\n"), size, maxBlockCount-1, id, mountPoint);
		size=maxBlockCount-1;
	}
	size*=SdBlockSize;

	if (!kernelFsAddBlockDeviceFile(kstrC(mountPoint), KernelFsBlockDeviceFormatFlatFile, size, &hwDeviceSdCardReaderReadFunctor, &hwDeviceSdCardReaderWriteFunctor, (void *)(uintptr_t)id)) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: could not add block device to VFS (id=%u, mountPoint='%s', size=%"PRIu32")\n"), id, mountPoint, size);
		sdQuit(&hwDevices[id].d.sdCardReader.sdCard);
		return false;
	}

	// Copy mount point
	hwDevices[id].d.sdCardReader.mountPoint=kstrC(mountPoint);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount success (id=%u, mountPoint='%s', size=%"PRIu32")\n"), id, mountPoint, size);

	return true;
}

void hwDeviceSdCardReaderUnmount(HwDeviceId id) {
	// Bad id?
	if (id>=HwDeviceIdMax)
		return;

	// Device slot not used for an SD card reader?
	if (hwDeviceGetType(id)!=HwDeviceTypeSdCardReader)
		return;

	// No card mounted using this device?
	if (hwDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return;

	// Grab local copy of mount point
	char mountPoint[KernelFsPathMax];
	kstrStrcpy(mountPoint, hwDevices[id].d.sdCardReader.mountPoint);

	// Write out cached block if valid but dirty
	if (hwDevices[id].d.sdCardReader.cacheIsValid && hwDevices[id].d.sdCardReader.cacheIsDirty && !sdWriteBlock(&hwDevices[id].d.sdCardReader.sdCard, hwDevices[id].d.sdCardReader.cacheBlock, hwDevices[id].d.sdCardReader.cache))
		kernelLog(LogTypeWarning, kstrP("SPI device SD card reader unmount: failed to write back dirty block %"PRIu32" (id=%u, mountPoint='%s')\n"), hwDevices[id].d.sdCardReader.cacheBlock, id, mountPoint);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("SPI device SD card reader unmount (id=%u, mountPoint='%s')\n"), id, mountPoint);

	// Remove virtual device file representing the card
	kernelFsFileDelete(mountPoint);

	// Power off SD card and mark unmounted
	sdQuit(&hwDevices[id].d.sdCardReader.sdCard);

	// Free memory
	kstrFree(&hwDevices[id].d.sdCardReader.mountPoint);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

KernelFsFileOffset hwDeviceSdCardReaderReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	HwDeviceId id=(HwDeviceId)(uintptr_t)userData;

	// Verify id is valid and that it represents an sd card reader device, with a mounted card.
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeSdCardReader || hwDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return 0;

	// Loop over addresses range reading bytes, reading new blocks as needed.
	KernelFsFileOffset readCount;
	for(readCount=0; readCount<len; ++readCount,++addr) {
		// Compute block number for the current address
		uint32_t block=addr/SdBlockSize;

		// If the existing block is valid, does not match the one we want, and is marked as being dirty,
		// then it needs writing back to disk now to prevent data loss.
		if (hwDevices[id].d.sdCardReader.cacheIsValid && block!=hwDevices[id].d.sdCardReader.cacheBlock && hwDevices[id].d.sdCardReader.cacheIsDirty) {
			// Attempt to write cached block to the card to avoid data loss
			if (!sdWriteBlock(&hwDevices[id].d.sdCardReader.sdCard, hwDevices[id].d.sdCardReader.cacheBlock, hwDevices[id].d.sdCardReader.cache))
				break; // Failed to save cache so we cannot re-use it.

			// Clear dirty flag
			hwDevices[id].d.sdCardReader.cacheIsDirty=false;
		}

		// Decide if we already have the data we need, or if we need to read from the card.
		if (!hwDevices[id].d.sdCardReader.cacheIsValid || block!=hwDevices[id].d.sdCardReader.cacheBlock) {
			// Attempt to read from card
			if (!sdReadBlock(&hwDevices[id].d.sdCardReader.sdCard, block, hwDevices[id].d.sdCardReader.cache)) {
				// Mark cache as invalid as a failed read can leave the cache clobbered
				hwDevices[id].d.sdCardReader.cacheIsValid=false;
				break;
			}

			// Update fields
			hwDevices[id].d.sdCardReader.cacheIsValid=true;
			hwDevices[id].d.sdCardReader.cacheIsDirty=false;
			hwDevices[id].d.sdCardReader.cacheBlock=block;
		}

		// Copy byte into users array
		assert(hwDevices[id].d.sdCardReader.cacheBlock==block);
		uint16_t offset=addr-block*SdBlockSize; // should be <512 so can fit in 16 bit not full 32
		data[readCount]=hwDevices[id].d.sdCardReader.cache[offset];
	}

	return readCount;
}

KernelFsFileOffset hwDeviceSdCardReaderWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	HwDeviceId id=(HwDeviceId)(uintptr_t)userData;

	// Verify id is valid and that it represents an sd card reader device, with a mounted card.
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeSdCardReader || hwDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return 0;

	// Loop over address range writing bytes, reading and writing blocks as required.
	KernelFsFileOffset writeCount=0;
	while(writeCount<len) {
		// Compute block number for the current address
		uint32_t block=addr/SdBlockSize;

		// Do we need to write back a dirty block before reading?
		if (hwDevices[id].d.sdCardReader.cacheIsValid && block!=hwDevices[id].d.sdCardReader.cacheBlock && hwDevices[id].d.sdCardReader.cacheIsDirty) {
			// Attempt to write block back to card
			if (!sdWriteBlock(&hwDevices[id].d.sdCardReader.sdCard, hwDevices[id].d.sdCardReader.cacheBlock, hwDevices[id].d.sdCardReader.cache))
				break;

			// Clear dirty flag
			hwDevices[id].d.sdCardReader.cacheIsDirty=false;
		}

		// Do we need to read a new block?
		if (!hwDevices[id].d.sdCardReader.cacheIsValid || block!=hwDevices[id].d.sdCardReader.cacheBlock) {
			// Attempt to read block from card
			if (!sdReadBlock(&hwDevices[id].d.sdCardReader.sdCard, block, hwDevices[id].d.sdCardReader.cache)) {
				// Mark cache as invalid as a failed read can leave the cache clobbered
				hwDevices[id].d.sdCardReader.cacheIsValid=false;
				break;
			}

			// Update fields
			hwDevices[id].d.sdCardReader.cacheIsValid=true;
			hwDevices[id].d.sdCardReader.cacheIsDirty=false;
			hwDevices[id].d.sdCardReader.cacheBlock=block;
		}

		// Overwrite parts of cached block with given data.
		assert(hwDevices[id].d.sdCardReader.cacheIsValid);
		assert(hwDevices[id].d.sdCardReader.cacheBlock==block);

		uint16_t offset=addr-block*SdBlockSize; // should be <512 so can fit in 16 bit not full 32
		uint16_t loopWriteLen=MIN(SdBlockSize-offset, len-writeCount);
		memcpy(hwDevices[id].d.sdCardReader.cache+offset, data+writeCount, loopWriteLen);

		hwDevices[id].d.sdCardReader.cacheIsDirty=true;

		// Update variables for next iteration
		writeCount+=loopWriteLen;
		addr+=loopWriteLen;
	}

	return writeCount;
}
