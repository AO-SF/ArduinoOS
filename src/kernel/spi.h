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

typedef enum {
#ifdef ARDUINO
	SpiClockSpeedDiv4  =(0u<<SPR1|0u<<SPR0),
	SpiClockSpeedDiv16 =(0u<<SPR1|1u<<SPR0),
	SpiClockSpeedDiv64 =(1u<<SPR1|0u<<SPR0),
	SpiClockSpeedDiv128=(1u<<SPR1|1u<<SPR0),
#else
	SpiClockSpeedDiv4,
	SpiClockSpeedDiv16,
	SpiClockSpeedDiv64,
	SpiClockSpeedDiv128,
#endif
} SpiClockSpeed;

bool spiInit(SpiClockSpeed clockSpeed); // Always succeeds on ARDUINO builds, fails otherwise.

uint8_t spiTransmitByte(uint8_t value);

uint8_t spiReadByte(void);

void spiWriteByte(uint8_t value);
void spiWriteStr(const char *str);
void spiWriteBlock(const uint8_t *data, size_t len);

#endif
