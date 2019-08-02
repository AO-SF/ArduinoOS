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

bool spiInit(void); // Always succeeds on ARDUINO builds, fails otherwise.

uint8_t spiTransmitByte(uint8_t value);

uint8_t spiReadByte(void);

void spiWriteByte(uint8_t value);
void spiWriteStr(const char *str);
void spiWriteBlock(const uint8_t *data, size_t len);

#endif
