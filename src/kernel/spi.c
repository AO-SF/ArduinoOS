#include "spi.h"

bool spiInit(SpiClockSpeed clockSpeed) {
#ifdef ARDUINO
	// Set MISO as input, MOSI and SCK as output.
	pinSetMode(SpiPinMiso, PinModeInput);
	pinSetMode(SpiPinMosi, PinModeOutput);
	pinSetMode(SpiPinSck, PinModeOutput);

	// Set following flags:
	// * SPE - enable hardware SPI
	// * MSTR - master mode
	// With MSB first data order implied due to lack of DORD flag, among others.
	SPCR|=(1<<SPE|1<<MSTR|clockSpeed);

	return true;
#else
	return false;
#endif
}

uint8_t spiTransmitByte(uint8_t value) {
#ifdef ARDUINO
	SPDR=value;
	while(!(SPSR&(1<<SPIF)))
		;
	return SPDR;
#else
	// shouldn't happen as spiInit would have returned false
	return 0;
#endif
}

uint8_t spiReadByte(void) {
	return spiTransmitByte(0);
}

void spiWriteByte(uint8_t value) {
	spiTransmitByte(value);
}

void spiWriteStr(const char *str) {
	do {
		spiWriteByte(*str);
	} while(*str++);
}

void spiWriteBlock(const uint8_t *data, size_t len) {
	for(size_t i=0; i<len; ++i)
		spiWriteByte(data[i]);
}
