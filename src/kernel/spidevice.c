#include <assert.h>
#include <string.h>

#include "kernelfs.h"
#include "log.h"
#include "pins.h"
#include "sd.h"
#include "spidevice.h"
#include "util.h"

typedef struct {
	uint8_t powerPin;
	uint8_t slaveSelectPin;
} SpiDevicePinPair;

const SpiDevicePinPair spiDevicePinPairs[SpiDeviceIdMax]={
	{.powerPin=PinD46, .slaveSelectPin=PinD47},
	{.powerPin=PinD48, .slaveSelectPin=PinD49},
};

typedef struct {
	KStr mountPoint;
	SdCard sdCard; // type set to SdTypeBadCard when none mounted
	uint8_t cache[SdBlockSize];
	uint32_t cacheIsValid:1; // if false, cacheBlock field is undefined as is the data in the cache array
	uint32_t cacheIsDirty:1; // if cache is valid, then this represents whether the cache array has been modified since reading
	uint32_t cacheBlock:30; // Note: using only 30 bits is safe as some bits of addresses are 'used up' by the fixed size 512 byte blocks, so not all 32 bits are needed (only 32-9=23 strictly needed)
} SpiDeviceSdCardReaderData;

typedef struct {
	SpiDeviceType type;
	union {
		SpiDeviceSdCardReaderData sdCardReader;
	} d;
} SpiDevice;

SpiDevice spiDevices[SpiDeviceIdMax];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

KernelFsFileOffset spiDeviceSdCardReaderReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset spiDeviceSdCardReaderWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void spiDeviceInit(void) {
	// Set all pins as output, with power pins low (no power) and slave select pins high (disabled).
	for(unsigned i=0; i<SpiDeviceIdMax; ++i) {
		pinSetMode(spiDevicePinPairs[i].powerPin, PinModeOutput);
		pinWrite(spiDevicePinPairs[i].powerPin, false);

		pinSetMode(spiDevicePinPairs[i].slaveSelectPin, PinModeOutput);
		pinWrite(spiDevicePinPairs[i].slaveSelectPin, true);
	}

	// Clear device table
	for(unsigned i=0; i<SpiDeviceIdMax; ++i) {
		spiDevices[i].type=SpiDeviceTypeUnused;
	}
}

bool spiDeviceRegister(SpiDeviceId id, SpiDeviceType type) {
	// Bad id or type?
	if (id>=SpiDeviceIdMax || type==SpiDeviceTypeUnused)
		return false;

	// Slot already in use?
	if (spiDevices[id].type!=SpiDeviceTypeUnused)
		return false;

	// Write to log
	kernelLog(LogTypeInfo, kstrP("registered SPI device id=%u type=%u\n"), id, type);

	// Set type to mark slot as used
	spiDevices[id].type=type;

	// Type-specific logic
	switch(type) {
		case SpiDeviceTypeUnused:
		break;
		case SpiDeviceTypeRaw:
		break;
		case SpiDeviceTypeSdCardReader:
			spiDevices[id].d.sdCardReader.sdCard.type=SdTypeBadCard;
		break;
	}

	return true;
}

void spiDeviceDeregister(SpiDeviceId id) {
	// Bad if?
	if (id>=SpiDeviceIdMax)
		return;

	// Slot not even in use?
	if (spiDevices[id].type==SpiDeviceTypeUnused)
		return;

	// Type-specific logic
	switch(spiDevices[id].type) {
		case SpiDeviceTypeUnused:
		break;
		case SpiDeviceTypeRaw:
		break;
		case SpiDeviceTypeSdCardReader:
			// We may have to unmount an SD card
			spiDeviceSdCardReaderUnmount(id);
		break;
	}

	// Force pins back to default to be safe
	pinWrite(spiDevicePinPairs[id].powerPin, false);
	pinWrite(spiDevicePinPairs[id].slaveSelectPin, true);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("deregistered SPI device id=%u type=%u\n"), id, spiDevices[id].type);

	// Clear type to mark slot as unused
	spiDevices[id].type=SpiDeviceTypeUnused;
}

SpiDeviceType spiDeviceGetType(SpiDeviceId id) {
	if (id>=SpiDeviceIdMax)
		return SpiDeviceTypeUnused;

	return spiDevices[id].type;
}

SpiDeviceId spiDeviceGetDeviceForPin(uint8_t pinNum) {
	for(unsigned i=0; i<SpiDeviceIdMax; ++i)
		if (pinNum==spiDevicePinPairs[i].powerPin || pinNum==spiDevicePinPairs[i].slaveSelectPin)
			return i;
	return SpiDeviceIdMax;
}

bool spiDeviceSdCardReaderMount(SpiDeviceId id, const char *mountPoint) {
	// Bad id?
	if (id>=SpiDeviceIdMax) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: bad id (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Device slot not used for an SD card reader?
	if (spiDeviceGetType(id)!=SpiDeviceTypeSdCardReader) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: bad device type (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Already have a card mounted using this device?
	if (spiDevices[id].d.sdCardReader.sdCard.type!=SdTypeBadCard) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: card already mounted (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Attempt to power on and initialise SD card
	SdInitResult sdInitRes=sdInit(&spiDevices[id].d.sdCardReader.sdCard, spiDevicePinPairs[id].powerPin, spiDevicePinPairs[id].slaveSelectPin);
	if (sdInitRes!=SdInitResultOk) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: sd init failed with %u (id=%u, mountPoint='%s')\n"), sdInitRes, id, mountPoint);
		return false;
	}

	// Mark cache block as undefined (before we even register read/write functors to be safe).
	spiDevices[id].d.sdCardReader.cacheIsValid=false;

	// Add block device at given point mount
	KernelFsFileOffset size=4096; // TODO: this properly - we have notes on how to find this from the card itself
	if (!kernelFsAddBlockDeviceFile(kstrC(mountPoint), KernelFsBlockDeviceFormatFlatFile, size, &spiDeviceSdCardReaderReadFunctor, &spiDeviceSdCardReaderWriteFunctor, (void *)(uintptr_t)id)) {
		kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount failed: could not add block device to VFS (id=%u, mountPoint='%s')\n"), id, mountPoint);
		sdQuit(&spiDevices[id].d.sdCardReader.sdCard);
		return false;
	}

	// Copy mount point
	spiDevices[id].d.sdCardReader.mountPoint=kstrC(mountPoint);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("SPI device SD card reader mount success (id=%u, mountPoint='%s')\n"), id, mountPoint);

	return true;
}

void spiDeviceSdCardReaderUnmount(SpiDeviceId id) {
	// Bad id?
	if (id>=SpiDeviceIdMax)
		return;

	// Device slot not used for an SD card reader?
	if (spiDeviceGetType(id)!=SpiDeviceTypeSdCardReader)
		return;

	// No card mounted using this device?
	if (spiDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return;

	// Grab local copy of mount point
	char mountPoint[KernelFsPathMax];
	kstrStrcpy(mountPoint, spiDevices[id].d.sdCardReader.mountPoint);

	// Write out cached block if valid but dirty
	if (spiDevices[id].d.sdCardReader.cacheIsValid && spiDevices[id].d.sdCardReader.cacheIsDirty && !sdWriteBlock(&spiDevices[id].d.sdCardReader.sdCard, spiDevices[id].d.sdCardReader.cacheBlock, spiDevices[id].d.sdCardReader.cache))
		kernelLog(LogTypeWarning, kstrP("SPI device SD card reader unmount: failed to write back dirty block %u (id=%u, mountPoint='%s')\n"), spiDevices[id].d.sdCardReader.cacheBlock, id, mountPoint);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("SPI device SD card reader unmount (id=%u, mountPoint='%s')\n"), id, mountPoint);

	// Remove virtual device file representing the card
	kernelFsFileDelete(mountPoint);

	// Power off SD card and mark unmounted
	sdQuit(&spiDevices[id].d.sdCardReader.sdCard);

	// Free memory
	kstrFree(&spiDevices[id].d.sdCardReader.mountPoint);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

KernelFsFileOffset spiDeviceSdCardReaderReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	SpiDeviceId id=(SpiDeviceId)(uintptr_t)userData;

	// Verify id is valid and that it represents an sd card reader device, with a mounted card.
	if (id>=SpiDeviceIdMax || spiDeviceGetType(id)!=SpiDeviceTypeSdCardReader || spiDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return 0;

	// Loop over addresses range reading bytes, reading new blocks as needed.
	KernelFsFileOffset readCount;
	for(readCount=0; readCount<len; ++readCount,++addr) {
		// Compute block number for the current address
		uint32_t block=addr/SdBlockSize;

		// If the existing block is valid, does not match the one we want, and is marked as being dirty,
		// then it needs writing back to disk now to prevent data loss.
		if (spiDevices[id].d.sdCardReader.cacheIsValid && block!=spiDevices[id].d.sdCardReader.cacheBlock && spiDevices[id].d.sdCardReader.cacheIsDirty) {
			// Attempt to write cached block to the card to avoid data loss
			if (!sdWriteBlock(&spiDevices[id].d.sdCardReader.sdCard, spiDevices[id].d.sdCardReader.cacheBlock, spiDevices[id].d.sdCardReader.cache))
				break; // Failed to save cache so we cannot re-use it.

			// Clear dirty flag
			spiDevices[id].d.sdCardReader.cacheIsDirty=false;
		}

		// Decide if we already have the data we need, or if we need to read from the card.
		if (!spiDevices[id].d.sdCardReader.cacheIsValid || block!=spiDevices[id].d.sdCardReader.cacheBlock) {
			// Attempt to read from card
			if (!sdReadBlock(&spiDevices[id].d.sdCardReader.sdCard, block, spiDevices[id].d.sdCardReader.cache)) {
				// Mark cache as invalid as a failed read can leave the cache clobbered
				spiDevices[id].d.sdCardReader.cacheIsValid=false;
				break;
			}

			// Update fields
			spiDevices[id].d.sdCardReader.cacheIsValid=true;
			spiDevices[id].d.sdCardReader.cacheIsDirty=false;
			spiDevices[id].d.sdCardReader.cacheBlock=block;
		}

		// Copy byte into users array
		assert(spiDevices[id].d.sdCardReader.cacheBlock==block);
		uint16_t offset=addr-block*SdBlockSize; // should be <512 so can fit in 16 bit not full 32
		data[readCount]=spiDevices[id].d.sdCardReader.cache[offset];
	}

	return readCount;
}

KernelFsFileOffset spiDeviceSdCardReaderWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	SpiDeviceId id=(SpiDeviceId)(uintptr_t)userData;

	// Verify id is valid and that it represents an sd card reader device, with a mounted card.
	if (id>=SpiDeviceIdMax || spiDeviceGetType(id)!=SpiDeviceTypeSdCardReader || spiDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return 0;

	// Loop over address range writing bytes, reading and writing blocks as required.
	KernelFsFileOffset writeCount=0;
	while(writeCount<len) {
		// Compute block number for the current address
		uint32_t block=addr/SdBlockSize;

		// Do we need to write back a dirty block before reading?
		if (spiDevices[id].d.sdCardReader.cacheIsValid && block!=spiDevices[id].d.sdCardReader.cacheBlock && spiDevices[id].d.sdCardReader.cacheIsDirty) {
			// Attempt to write block back to card
			if (!sdWriteBlock(&spiDevices[id].d.sdCardReader.sdCard, spiDevices[id].d.sdCardReader.cacheBlock, spiDevices[id].d.sdCardReader.cache))
				break;

			// Clear dirty flag
			spiDevices[id].d.sdCardReader.cacheIsDirty=false;
		}

		// Do we need to read a new block?
		if (!spiDevices[id].d.sdCardReader.cacheIsValid || block!=spiDevices[id].d.sdCardReader.cacheBlock) {
			// Attempt to read block from card
			if (!sdReadBlock(&spiDevices[id].d.sdCardReader.sdCard, block, spiDevices[id].d.sdCardReader.cache)) {
				// Mark cache as invalid as a failed read can leave the cache clobbered
				spiDevices[id].d.sdCardReader.cacheIsValid=false;
				break;
			}

			// Update fields
			spiDevices[id].d.sdCardReader.cacheIsValid=true;
			spiDevices[id].d.sdCardReader.cacheIsDirty=false;
			spiDevices[id].d.sdCardReader.cacheBlock=block;
		}

		// Overwrite parts of cached block with given data.
		assert(spiDevices[id].d.sdCardReader.cacheIsValid);
		assert(spiDevices[id].d.sdCardReader.cacheBlock==block);

		uint16_t offset=addr-block*SdBlockSize; // should be <512 so can fit in 16 bit not full 32
		uint16_t loopWriteLen=MIN(SdBlockSize-offset, len-writeCount);
		memcpy(spiDevices[id].d.sdCardReader.cache+offset, data+writeCount, loopWriteLen);

		spiDevices[id].d.sdCardReader.cacheIsDirty=true;

		// Update variables for next iteration
		writeCount+=loopWriteLen;
		addr+=loopWriteLen;
	}

	return writeCount;
}
