#ifdef ARDUINO

#include <avr/io.h>
#include <stdlib.h>

#include "pins.h"

#define PinsGroupMax 12
volatile uint8_t *pinsArrayPin[PinsGroupMax]={&PINA, &PINB, &PINC, &PIND, &PINE, &PINF, &PING, &PINH, NULL, &PINJ, &PINK, &PINL};
volatile uint8_t *pinsArrayDdr[PinsGroupMax]={&DDRA, &DDRB, &DDRC, &DDRD, &DDRE, &DDRF, &DDRG, &DDRH, NULL, &DDRJ, &DDRK, &DDRL};
volatile uint8_t *pinsArrayPort[PinsGroupMax]={&PORTA, &PORTB, &PORTC, &PORTD, &PORTE, &PORTF, &PORTG, &PORTH, NULL, &PORTJ, &PORTK, &PORTL};

#define PinNumGetGroup(pinNum) ((pinNum)>>3)
#define PinNumGetShift(pinNum) ((pinNum)&7)

#define PinNB 96
const uint8_t pinsValidArray[PinNB]={
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //  0-15
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, // 16-31
	1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, // 32-47
	1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, // 48-63
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, // 64-79
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 80-95
};

bool pinRead(uint8_t pinNum) {
	if (pinNum>=PinNB)
		return false;
	*pinsArrayDdr[PinNumGetGroup(pinNum)]&=~(1u<<PinNumGetShift(pinNum));
	return ((*pinsArrayPin[PinNumGetGroup(pinNum)])&(1u<<PinNumGetShift(pinNum)))!=0;
}

bool pinWrite(uint8_t pinNum, bool value) {
	if (pinNum>=PinNB)
		return false;
	*pinsArrayDdr[PinNumGetGroup(pinNum)]|=(1u<<PinNumGetShift(pinNum));
	if (value!=0)
		*pinsArrayPort[PinNumGetGroup(pinNum)]|=(1u<<PinNumGetShift(pinNum));
	else
		*pinsArrayPort[PinNumGetGroup(pinNum)]&=~(1u<<PinNumGetShift(pinNum));
	return true;
}

#endif
