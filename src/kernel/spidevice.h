#ifndef SPIDEVICE_H
#define SPIDEVICE_H

#include <stdint.h>

typedef enum {
	SpiDeviceTypeUnused,
	SpiDeviceTypeRaw, // let the user write to the two associated pins
	SpiDeviceTypeSdCardReader,
} SpiDeviceType;

typedef uint8_t SpiDeviceId;
#define SpiDeviceIdMax 4

void spiDeviceInit(void); // should be one of the very first things to be called during kernelBoot (used to ensure pins are output and have the correct state asap)

SpiDeviceType spiDeviceGetType(SpiDeviceId id);

SpiDeviceId spiDeviceGetDeviceForPin(uint8_t pinNum); // returns SpiDeviceIdMax if given pin is not associated with any device (used or unused)

#endif
