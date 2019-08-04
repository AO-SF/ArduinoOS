#include "pins.h"
#include "spidevice.h"

#define SpiDeviceMax 4

typedef struct {
	uint8_t powerPin;
	uint8_t slaveSelectPin;
} SpiDevicePinPair;

const SpiDevicePinPair spiDevicePinPairs[SpiDeviceMax]={
	{.powerPin=PinD42, .slaveSelectPin=PinD43},
	{.powerPin=PinD44, .slaveSelectPin=PinD46},
	{.powerPin=PinD46, .slaveSelectPin=PinD47},
	{.powerPin=PinD48, .slaveSelectPin=PinD49},
};

typedef struct {
	SpiDeviceType type;
} SpiDevice;

SpiDevice spiDevices[SpiDeviceMax];

void spiDeviceInit(void) {
	// Set all pins as output, with power pins low (no power) and slave select pins high (disabled).
	for(unsigned i=0; i<SpiDeviceMax; ++i) {
		pinSetMode(spiDevicePinPairs[i].powerPin, PinModeOutput);
		pinWrite(spiDevicePinPairs[i].powerPin, false);

		pinSetMode(spiDevicePinPairs[i].slaveSelectPin, PinModeOutput);
		pinWrite(spiDevicePinPairs[i].slaveSelectPin, true);
	}

	// Clear device table
	for(unsigned i=0; i<SpiDeviceMax; ++i) {
		spiDevices[i].type=SpiDeviceTypeUnused;
	}
}
