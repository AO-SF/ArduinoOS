#ifdef ARDUINO
#include <avr/io.h>
#endif
#include <stdlib.h>

#include "log.h"
#include "pins.h"
#include "util.h"

const uint8_t pinsArrayValid[PinGroupMax]={
	0xFF, 0xFF, //  0-15
	0xFF, 0x8F, // 16-31
	0x3B, 0xFF, // 32-47
	0x27, 0x7B, // 48-63
	0x00, 0x03, // 64-79
	0xFF, 0xFF, // 80-95
};

#ifdef ARDUINO
volatile uint8_t *pinsArrayPin[PinGroupMax]={&PINA, &PINB, &PINC, &PIND, &PINE, &PINF, &PING, &PINH, NULL, &PINJ, &PINK, &PINL};
volatile uint8_t *pinsArrayDdr[PinGroupMax]={&DDRA, &DDRB, &DDRC, &DDRD, &DDRE, &DDRF, &DDRG, &DDRH, NULL, &DDRJ, &DDRK, &DDRL};
volatile uint8_t *pinsArrayPort[PinGroupMax]={&PORTA, &PORTB, &PORTC, &PORTD, &PORTE, &PORTF, &PORTG, &PORTH, NULL, &PORTJ, &PORTK, &PORTL};
#else
bool pinStates[PinMax]={0};
#endif

bool pinIsValid(uint8_t pinNum) {
	if (pinNum>=PinMax)
		return false;
	return (pinsArrayValid[PinNumGetGroup(pinNum)]>>PinNumGetShift(pinNum))&1;
}

void pinSetMode(uint8_t pinNum, PinMode mode) {
	if (!pinIsValid(pinNum)) {
		kernelLog(LogTypeWarning, kstrP("bad pin %u in setmode\n"), pinNum);
		return;
	}
#ifdef ARDUINO
	switch(mode) {
		case PinModeInput:
			*pinsArrayDdr[PinNumGetGroup(pinNum)]&=~(1u<<PinNumGetShift(pinNum));
		break;
		case PinModeOutput:
			*pinsArrayDdr[PinNumGetGroup(pinNum)]|=(1u<<PinNumGetShift(pinNum));
		break;
	}
#endif
// Null-op on PC
}

bool pinRead(uint8_t pinNum) {
	if (!pinIsValid(pinNum)) {
		kernelLog(LogTypeWarning, kstrP("bad pin %u in read\n"), pinNum);
		return false;
	}
#ifdef ARDUINO
	return ((*pinsArrayPin[PinNumGetGroup(pinNum)])&(1u<<PinNumGetShift(pinNum)))!=0;
#else
	return pinStates[pinNum];
#endif
}

bool pinWrite(uint8_t pinNum, bool value) {
	if (!pinIsValid(pinNum)) {
		kernelLog(LogTypeWarning, kstrP("bad pin %u in write\n"), pinNum);
		return false;
	}
#ifdef ARDUINO
	if (value!=0)
		*pinsArrayPort[PinNumGetGroup(pinNum)]|=(1u<<PinNumGetShift(pinNum));
	else
		*pinsArrayPort[PinNumGetGroup(pinNum)]&=~(1u<<PinNumGetShift(pinNum));
#else
	pinStates[pinNum]=(value!=0);
#endif
	return true;
}
