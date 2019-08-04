#include "pins.h"
#include "spidevice.h"

typedef struct {
	uint8_t powerPin;
	uint8_t slaveSelectPin;
} SpiDevicePinPair;

const SpiDevicePinPair spiDevicePinPairs[SpiDeviceIdMax]={
	{.powerPin=PinD42, .slaveSelectPin=PinD43},
	{.powerPin=PinD44, .slaveSelectPin=PinD46},
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

SpiDeviceType spiDeviceGetType(SpiDeviceId id) {
	return spiDevices[id].type;
}

SpiDeviceId spiDeviceGetDeviceForPin(uint8_t pinNum) {
	for(unsigned i=0; i<SpiDeviceIdMax; ++i)
		if (pinNum==spiDevicePinPairs[i].powerPin || pinNum==spiDevicePinPairs[i].slaveSelectPin)
			return i;
	return SpiDeviceIdMax;
}
