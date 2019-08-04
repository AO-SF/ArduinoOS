#include "pins.h"
#include "spidevice.h"

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
	SpiDeviceType type;
} SpiDevice;

SpiDevice spiDevices[SpiDeviceIdMax];

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

	// Set type to mark slot as used
	spiDevices[id].type=type;

	return true;
}

void spiDeviceDeregister(SpiDeviceId id) {
	// Bad if?
	if (id>=SpiDeviceIdMax)
		return;

	// Slot not even in use?
	if (spiDevices[id].type==SpiDeviceTypeUnused)
		return;

	// Force pins back to default just to be safe
	pinWrite(spiDevicePinPairs[id].powerPin, false);
	pinWrite(spiDevicePinPairs[id].slaveSelectPin, true);

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
