#ifndef SPI_H
#define SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ARDUINO
#include <avr/io.h>
#endif

#include "pins.h"

#define SpiPinMiso PinD50
#define SpiPinMosi PinD51
#define SpiPinSck PinD52
#define SpiPinSlaveSelect PinD53

typedef enum {
	SpiClockSpeedDiv4,
	SpiClockSpeedDiv16,
	SpiClockSpeedDiv64,
	SpiClockSpeedDiv128,
} SpiClockSpeed;

void spiInit(SpiClockSpeed clockSpeed);

// Note: the following functions should only be used directly from kernel space if the SPI bus is 'locked' first - see kernelSpiGrabLock.

uint8_t spiTransmitByte(uint8_t value);

uint8_t spiReadByte(void);

void spiWriteByte(uint8_t value);
void spiWriteStr(const char *str);
void spiWriteBlock(const uint8_t *data, size_t len);

bool spiIsReservedPin(uint8_t pinNum); // is the given pin a one used by the SPI bus?

#endif
