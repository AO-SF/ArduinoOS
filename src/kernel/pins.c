#ifdef ARDUINO

#include <avr/io.h>
#include <stdlib.h>

#include "pins.h"

#define PinsGroupMax 12
volatile uint8_t *pinsArrayDdr[PinsGroupMax]={&DDRA, &DDRB, &DDRC, &DDRD, &DDRE, &DDRF, &DDRG, &DDRH, NULL, &DDRJ, &DDRK, &DDRL};
volatile uint8_t *pinsArrayPort[PinsGroupMax]={&PORTA, &PORTB, &PORTC, &PORTD, &PORTE, &PORTF, &PORTG, &PORTH, NULL, &PORTJ, &PORTK, &PORTL};

#define PinNumGetGroup(pinNum) ((pinNum)>>3)
#define PinNumGetShift(pinNum) ((pinNum)&7)

bool pinRead(uint8_t pinNum) {
	// TODO: this
	return false;
}

void pinWrite(uint8_t pinNum, bool value) {
	*pinsArrayDdr[PinNumGetGroup(pinNum)]|=(1u<<PinNumGetShift(pinNum));
	if (value!=0)
		*pinsArrayPort[PinNumGetGroup(pinNum)]|=(1u<<PinNumGetShift(pinNum));
	else
		*pinsArrayPort[PinNumGetGroup(pinNum)]&=~(1u<<PinNumGetShift(pinNum));
}

#endif
