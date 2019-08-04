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
	{.powerPin=PinD42, .slaveSelectPin=PinD43},
	{.powerPin=PinD44, .slaveSelectPin=PinD45},
	{.powerPin=PinD46, .slaveSelectPin=PinD47},
	{.powerPin=PinD48, .slaveSelectPin=PinD49},
};

typedef struct {
	KStr mountPoint;
	SdCard sdCard; // type set to SdTypeBadCard when none mounted
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
	uint8_t scratchBuf[SdBlockSize];

	SpiDeviceId id=(SpiDeviceId)(uintptr_t)userData;

	// Verify id is valid and that it represents an sd card reader device, with a mounted card.
	if (id>=SpiDeviceIdMax || spiDeviceGetType(id)!=SpiDeviceTypeSdCardReader || spiDevices[id].d.sdCardReader.sdCard.type==SdTypeBadCard)
		return 0; // TODO: or -1 for error?

	// First read up to first block boundary (or less if len is low enough).
	KernelFsFileOffset readCount;
	uint16_t lastBlock=UINT16_MAX; // TODO: better (whole thing)
	for(readCount=0; readCount<len; ++readCount,++addr) {
		// Do we need to read a new block?
		uint16_t block=addr/SdBlockSize;
		if (block!=lastBlock && !sdReadBlock(&spiDevices[id].d.sdCardReader.sdCard, block, scratchBuf))
			break;
		lastBlock=block;

		// Copy byte into users array
		uint16_t offset=addr-block*SdBlockSize;
		data[readCount]=scratchBuf[offset];
	}

	return readCount;
}

KernelFsFileOffset spiDeviceSdCardReaderWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	SpiDeviceId id=(SpiDeviceId)(uintptr_t)userData;
	_unused(id);

	// TODO: implement this
	return 0;
}
