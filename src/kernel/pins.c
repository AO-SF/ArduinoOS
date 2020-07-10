#ifdef ARDUINO
#include <avr/io.h>
#endif
#include <stdlib.h>

#include "log.h"
#include "pins.h"

const uint8_t pinsValidArray[PinNB]={
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //  0-15
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, // 16-31
	1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, // 32-47
	1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, // 48-63
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, // 64-79
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 80-95
};

uint8_t pinsUsedArray[PinNB]={
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //  0-15
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 16-31
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 32-47
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 48-63
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, // 64-79
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 80-95
};

#ifdef ARDUINO
#define PinsGroupMax 12
volatile uint8_t *pinsArrayPin[PinsGroupMax]={&PINA, &PINB, &PINC, &PIND, &PINE, &PINF, &PING, &PINH, NULL, &PINJ, &PINK, &PINL};
volatile uint8_t *pinsArrayDdr[PinsGroupMax]={&DDRA, &DDRB, &DDRC, &DDRD, &DDRE, &DDRF, &DDRG, &DDRH, NULL, &DDRJ, &DDRK, &DDRL};
volatile uint8_t *pinsArrayPort[PinsGroupMax]={&PORTA, &PORTB, &PORTC, &PORTD, &PORTE, &PORTF, &PORTG, &PORTH, NULL, &PORTJ, &PORTK, &PORTL};

#define PinNumGetGroup(pinNum) ((pinNum)>>3)
#define PinNumGetShift(pinNum) ((pinNum)&7)

#else

bool pinStates[PinNB]={0};

#endif

bool pinIsValid(uint8_t pinNum) {
	// Out of range?
	if (pinNum>=PinNB)
		return false;

	// Use lookup array
	return pinsValidArray[pinNum];
}

bool pinGrab(uint8_t pinNum) {
	if (!pinIsValid(pinNum))
		return false;

	if (pinInUse(pinNum))
		return false;

	pinsUsedArray[pinNum]=true;

	return true;
}

void pinRelease(uint8_t pinNum) {
	if (!pinIsValid(pinNum))
		return;

	pinsUsedArray[pinNum]=false;
}

bool pinInUse(uint8_t pinNum) {
	if (!pinIsValid(pinNum))
		return false;

	return pinsUsedArray[pinNum];
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

void pinsDebug(void) {
	kernelLog(LogTypeInfo, kstrP("Pins Info:\n"));
	for(unsigned i=0; i<PinNB; ++i) {
		if (!pinIsValid(i))
			continue;

		kernelLog(LogTypeInfo, kstrP("	%u - state=%u (%s)\n"), i, pinRead(i), (pinInUse(i) ? "used" : "free"));
	}
}
