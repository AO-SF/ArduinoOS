#ifndef SPIDEVICE_H
#define SPIDEVICE_H

typedef enum {
	SpiDeviceTypeUnused,
	SpiDeviceTypeRaw, // let the user write to the two associated pins
	SpiDeviceTypeSdCardReader,
} SpiDeviceType;

void spiDeviceInit(void); // should be one of the very first things to be called during kernelBoot (used to ensure pins are output and have the correct state asap)

#endif
