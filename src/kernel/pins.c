#ifdef ARDUINO

#include <avr/io.h>

#include "pins.h"

bool pinRead(uint8_t pinNum) {
	// TODO: this
	return 0;
}

void pinWrite(uint8_t pinNum, bool value) {
	// TODO: Support pins other than 13 for the LED
	DDRB|=(1<<PB7);
	if (value!=0)
		PORTB|=(1<<PB7);
	else
		PORTB&=~(1<<PB7);
}

#endif
