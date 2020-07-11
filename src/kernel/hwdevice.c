#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#ifdef ARDUINO
#include <avr/pgmspace.h>
#include <stddef.h>
#endif

#include "kernelfs.h"
#include "ktime.h"
#include "log.h"
#include "pins.h"
#include "sd.h"
#include "hwdevice.h"
#include "util.h"

typedef struct {
	KTime lastReadTime;
	int16_t temperature;
	int16_t humitity;
} HwDeviceDht22Data;

typedef struct {
	KStr mountPoint;
	SdCard sdCard; // type set to SdTypeBadCard when none mounted
	uint8_t *cache; // malloc'd, SdBlockSize in size
	uint32_t cacheIsValid:1; // if false, cacheBlock field is undefined as is the data in the cache array
	uint32_t cacheIsDirty:1; // if cache is valid, then this represents whether the cache array has been modified since reading
	uint32_t cacheBlock:30; // Note: using only 30 bits is safe as some bits of addresses are 'used up' by the fixed size 512 byte blocks, so not all 32 bits are needed (only 32-9=23 strictly needed)
} HwDeviceSdCardReaderData;

STATICASSERT(HwDeviceTypeBits<=8);
typedef struct {
	uint8_t type;
	uint8_t pins[HwDevicePinsMax];
	union {
		HwDeviceSdCardReaderData sdCardReader;
		HwDeviceDht22Data dht22;
	} d;
} HwDevice;

HwDevice hwDevices[HwDeviceIdMax];

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t hwDeviceSdCardReaderFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
bool hwDeviceSdCardReaderFlushFunctor(void *userData);
KernelFsFileOffset hwDeviceSdCardReaderReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset hwDeviceSdCardReaderWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void hwDeviceInit(void) {
	// Ensure all pins which have not been reserved by the kernel are set to default state (output, low)
	for(unsigned pinNum=0; pinNum<PinNB; ++pinNum) {
		if (!pinIsValid(pinNum) || pinInUse(pinNum))
			continue;
		pinWrite(pinNum, false);
		pinSetMode(pinNum, PinModeOutput);
	}

	// Clear device table
	for(unsigned i=0; i<HwDeviceIdMax; ++i)
		hwDevices[i].type=HwDeviceTypeUnused;
}

void hwDeviceTick(void) {
	for(unsigned i=0; i<HwDeviceIdMax; ++i) {
		HwDevice *device=&hwDevices[i];
		switch(device->type) {
			case HwDeviceTypeUnused:
			case HwDeviceTypeRaw:
			case HwDeviceTypeSdCardReader:
				// Nothing to do
			break;
			case HwDeviceTypeDht22: {
				// Not yet time to read values again?
				if (ktimeGetMonotonicMs()-device->d.dht22.lastReadTime<2000) // wait at least 2s between reads
					break;

				// Update values
				hwDeviceDht22Read(i);
			} break;
		}
	}
}

STATICASSERT(HwDevicePinsMax==4); // For hwDeviceRegister logging logic
bool hwDeviceRegister(HwDeviceId id, HwDeviceType type, const uint8_t *pins) {
	// Bad id or type?
	if (id>=HwDeviceIdMax || type==HwDeviceTypeUnused)
		return false;

	// Slot already in use?
	if (hwDevices[id].type!=HwDeviceTypeUnused)
		return false;

	// Set type to mark slot as used and set pins array to invalid initally
	hwDevices[id].type=type;

	unsigned pinCount=hwDeviceTypeGetPinCount(hwDevices[id].type);
	for(unsigned i=0; i<pinCount; ++i)
		hwDevices[id].pins[i]=PinInvalid;

	// Attempt to grab the given pins
	// Also set them to output low in case they are not already
	for(unsigned i=0; i<pinCount; ++i) {
		// Attempt to grab pin
		if (!pinGrab(pins[i])) {
			kernelLog(LogTypeInfo, kstrP("could not register HW device id=%u type=%u - could not grab pin %u\n"), id, type, pins[i]);
			goto error;
		}

		// Update device pins array
		hwDevices[id].pins[i]=pins[i];

		// Set pin to low then output for safety
		pinWrite(pins[i], false);
		pinSetMode(pins[i], PinModeOutput);
	}

	// Type-specific logic
	switch(type) {
		case HwDeviceTypeUnused:
		break;
		case HwDeviceTypeRaw:
		break;
		case HwDeviceTypeSdCardReader:
			hwDevices[id].d.sdCardReader.cache=malloc(SdBlockSize);
			if (hwDevices[id].d.sdCardReader.cache==NULL) {
				kernelLog(LogTypeInfo, kstrP("could not register HW device id=%u type=%u - could not allocate cache of size %u\n"), id, type, SdBlockSize);
				goto error;
			}
			hwDevices[id].d.sdCardReader.sdCard.type=SdTypeBadCard;
		break;
		case HwDeviceTypeDht22:
			// Set data pin to input mode and then turn on power pin to enable device
			pinWrite(hwDeviceDht22GetDataPin(id), false);
			pinSetMode(hwDeviceDht22GetDataPin(id), PinModeInput);
			pinWrite(hwDeviceDht22GetPowerPin(id), true);

			// Set initial values to 0 to indicate we need to read (we have to wait at least 2s after poweringon to read values, so this is the best we can do).
			hwDevices[id].d.dht22.temperature=0;
			hwDevices[id].d.dht22.humitity=0;
			hwDevices[id].d.dht22.lastReadTime=0;
		break;
	}

	// Write to log
	// TODO: think of a better way of handling pins so that we do not require static assert (above) and switch statement
	switch(pinCount) {
		case 0: kernelLog(LogTypeInfo, kstrP("registered HW device id=%u type=%u\n"), id, type); break;
		case 1: kernelLog(LogTypeInfo, kstrP("registered HW device id=%u type=%u, pins=[%u]\n"), id, type, pins[0]); break;
		case 2: kernelLog(LogTypeInfo, kstrP("registered HW device id=%u type=%u, pins=[%u,%u]\n"), id, type, pins[0], pins[1]); break;
		case 3: kernelLog(LogTypeInfo, kstrP("registered HW device id=%u type=%u, pins=[%u,%u,%u]\n"), id, type, pins[0], pins[1], pins[2]); break;
		case 4: kernelLog(LogTypeInfo, kstrP("registered HW device id=%u type=%u, pins=[%u,%u,%u,%u]\n"), id, type, pins[0], pins[1], pins[2], pins[3]); break;
	}

	return true;

	error:
	hwDevices[id].type=HwDeviceTypeUnused;
	for(unsigned i=0; i<pinCount; ++i) {
		uint8_t pinNum=hwDevices[id].pins[i];
		if (pinNum==PinInvalid)
			continue;
		pinWrite(pinNum, false);
		pinSetMode(pinNum, PinModeOutput);
		pinRelease(pinNum);
	}
	return false;
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
		case HwDeviceTypeDht22:
			// Nothing to do (power pin is turned off below which disables the device)
		break;
	}

	// Force pins back to default to be safe (output low), then release them
	unsigned pinCount=hwDeviceTypeGetPinCount(hwDevices[id].type);
	for(unsigned i=0; i<pinCount; ++i) {
		uint8_t pinNum=hwDeviceGetPinN(id, i);
		if (pinNum==PinInvalid)
			continue;
		pinWrite(pinNum, false);
		pinSetMode(pinNum, PinModeOutput);
		pinRelease(pinNum);
	}

	// Write to log
	kernelLog(LogTypeInfo, kstrP("deregistered HW device id=%u type=%u\n"), id, hwDevices[id].type);

	// Clear type to mark slot as unused
	hwDevices[id].type=HwDeviceTypeUnused;
}

HwDeviceType hwDeviceGetType(HwDeviceId id) {
	if (id>=HwDeviceIdMax)
		return HwDeviceTypeUnused;

	return hwDevices[id].type;
}

uint8_t hwDeviceGetPinN(HwDeviceId id, unsigned n) {
	if (id>=HwDeviceIdMax)
		return PinInvalid;

	if (n>=hwDeviceTypeGetPinCount(hwDevices[id].type))
		return PinInvalid;

	return hwDevices[id].pins[n];
}

uint8_t hwDeviceSdCardReaderGetPowerPin(HwDeviceId id) {
	return hwDeviceGetPinN(id, 0);
}

uint8_t hwDeviceSdCardReaderGetSlaveSelectPin(HwDeviceId id) {
	return hwDeviceGetPinN(id, 1);
}

bool hwDeviceSdCardReaderMount(HwDeviceId id, const char *mountPoint) {
	// Bad id?
	if (id>=HwDeviceIdMax) {
		kernelLog(LogTypeInfo, kstrP("HW device SD card reader mount failed: bad id (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Device slot not used for an SD card reader?
	if (hwDeviceGetType(id)!=HwDeviceTypeSdCardReader) {
		kernelLog(LogTypeInfo, kstrP("HW device SD card reader mount failed: bad device type (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Already have a card mounted using this device?
	if (hwDevices[id].d.sdCardReader.sdCard.type!=SdTypeBadCard) {
		kernelLog(LogTypeInfo, kstrP("HW device SD card reader mount failed: card already mounted (id=%u, mountPoint='%s')\n"), id, mountPoint);
		return false;
	}

	// Attempt to power on and initialise SD card
	SdInitResult sdInitRes=sdInit(&hwDevices[id].d.sdCardReader.sdCard, hwDeviceSdCardReaderGetPowerPin(id), hwDeviceSdCardReaderGetSlaveSelectPin(id));
	if (sdInitRes!=SdInitResultOk) {
		kernelLog(LogTypeInfo, kstrP("HW device SD card reader mount failed: sd init failed with %u (id=%u, mountPoint='%s')\n"), sdInitRes, id, mountPoint);
		return false;
	}

	// Mark cache block as undefined (before we even register read/write functors to be safe).
	hwDevices[id].d.sdCardReader.cacheIsValid=false;

	// Add block device at given point mount
	uint32_t maxBlockCount=(((uint32_t)1u)<<(32-SdBlockSizeBits)); // we are limited by 32 bit addresses, regardless of how large blocks are

	KernelFsFileOffset size=hwDevices[id].d.sdCardReader.sdCard.blockCount;
	if (size>=maxBlockCount) {
		kernelLog(LogTypeWarning, kstrP("HW device SD card reader mount: block count too large, reducing from %"PRIu32" to %"PRIu32" (id=%u, mountPoint='%s')\n"), size, maxBlockCount-1, id, mountPoint);
		size=maxBlockCount-1;
	}
	size*=SdBlockSize;

	if (!kernelFsAddBlockDeviceFile(kstrC(mountPoint), &hwDeviceSdCardReaderFsFunctor, (void *)(uintptr_t)id, KernelFsBlockDeviceFormatFlatFile, size, true)) {
		kernelLog(LogTypeInfo, kstrP("HW device SD card reader mount failed: could not add block device to VFS (id=%u, mountPoint='%s', size=%"PRIu32")\n"), id, mountPoint, size);
		sdQuit(&hwDevices[id].d.sdCardReader.sdCard);
		return false;
	}

	// Copy mount point
	hwDevices[id].d.sdCardReader.mountPoint=kstrC(mountPoint);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("HW device SD card reader mount success (id=%u, mountPoint='%s', size=%"PRIu32")\n"), id, mountPoint, size);

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
		kernelLog(LogTypeWarning, kstrP("HW device SD card reader unmount: failed to write back dirty block %"PRIu32" (id=%u, mountPoint='%s')\n"), hwDevices[id].d.sdCardReader.cacheBlock, id, mountPoint);

	// Write to log
	kernelLog(LogTypeInfo, kstrP("HW device SD card reader unmount (id=%u, mountPoint='%s')\n"), id, mountPoint);

	// Remove virtual device file representing the card
	kernelFsFileDelete(mountPoint);

	// Power off SD card and mark unmounted
	sdQuit(&hwDevices[id].d.sdCardReader.sdCard);

	// Free memory
	kstrFree(&hwDevices[id].d.sdCardReader.mountPoint);
}

uint8_t hwDeviceDht22GetPowerPin(HwDeviceId id) {
	return hwDeviceGetPinN(id, 0);
}

uint8_t hwDeviceDht22GetDataPin(HwDeviceId id) {
	return hwDeviceGetPinN(id, 1);
}

int16_t hwDeviceDht22GetTemperature(HwDeviceId id) {
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeDht22)
		return 0;
	return hwDevices[id].d.dht22.temperature;
}

int16_t hwDeviceDht22GetHumidity(HwDeviceId id) {
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeDht22)
		return 0;
	return hwDevices[id].d.dht22.humitity;
}

uint32_t hwDeviceDht22GetLastReadTime(HwDeviceId id) {
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeDht22)
		return 0;
	return hwDevices[id].d.dht22.lastReadTime;
}

unsigned hwDeviceTypeGetPinCount(HwDeviceType type) {
	switch(type) {
		case HwDeviceTypeUnused:
			return 0;
		break;
		case HwDeviceTypeRaw:
			return HwDevicePinsMax;
		break;
		case HwDeviceTypeSdCardReader:
			return 2; // power + slave select (SPI pins are general do not count towards this particular HW device)
		break;
		case HwDeviceTypeDht22:
			return 2; // power + data
		break;
	}
	return 0;
}

bool hwDeviceDht22Read(HwDeviceId id) {
	// Check device is actually registered as a DHT22 sensor
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeDht22)
		return false;

	// Attempt to read data
	uint8_t pin=hwDeviceDht22GetDataPin(id);

	pinWrite(pin, false);
	pinSetMode(pin, PinModeOutput);
	ktimeDelayUs(600);
	pinSetMode(pin, PinModeInput);

	ktimeDelayUs(70);
	if (pinRead(pin))
		return false;

	ktimeDelayUs(80);
	if (!pinRead(pin))
		return false;

	uint8_t buffer[5];
	for(uint8_t b=0; b<5; ++b) {
		uint8_t inByte=0;
		for (uint8_t i=0; i<8; ++i)	{
			uint8_t toCount=0;
			while(pinRead(pin)) {
				ktimeDelayUs(2);
				if (toCount++>25)
					return false;
			}
			ktimeDelayUs(5);

			toCount=0;
			while(!pinRead(pin)) {
				ktimeDelayUs(2);
				if (toCount++>28)
					return false;
			}
			ktimeDelayUs(50);

			inByte<<=1;
			if (pinRead(pin)) // read bit
				inByte|=1;
		}
		buffer[b]=inByte;
	}

	// Verify checksum is correct
	uint8_t checksum=buffer[0]+buffer[1]+buffer[2]+buffer[3];
	if (buffer[4]!=checksum)
		return false;

	// Read humidity and temperature values
	hwDevices[id].d.dht22.humitity=(((uint16_t)buffer[0])<<8)|buffer[1]; // note: datasheet is very misleading here
	hwDevices[id].d.dht22.temperature=(((uint16_t)buffer[2])<<8)|buffer[3];

	// Successful read - update last read time
	hwDevices[id].d.dht22.lastReadTime=ktimeGetMonotonicMs();

	return true;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint32_t hwDeviceSdCardReaderFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return hwDeviceSdCardReaderFlushFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
			return hwDeviceSdCardReaderReadFunctor(addr, data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
			return hwDeviceSdCardReaderWriteFunctor(addr, data, len, userData);
		break;
	}

	assert(false);
	return 0;
}

bool hwDeviceSdCardReaderFlushFunctor(void *userData) {
	HwDeviceId id=(HwDeviceId)(uintptr_t)userData;

	// Verify id is valid and that it represents an sd card reader device, with a mounted card.
	if (id>=HwDeviceIdMax || hwDeviceGetType(id)!=HwDeviceTypeSdCardReader || hwDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return false;

	// Nothing to do? (invalid or clean cache)
	if (!hwDevices[id].d.sdCardReader.cacheIsValid || !hwDevices[id].d.sdCardReader.cacheIsDirty)
		return true;

	// Write out cached block
	if (!sdWriteBlock(&hwDevices[id].d.sdCardReader.sdCard, hwDevices[id].d.sdCardReader.cacheBlock, hwDevices[id].d.sdCardReader.cache))
		return false;

	hwDevices[id].d.sdCardReader.cacheIsDirty=false;

	return true;
}

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
