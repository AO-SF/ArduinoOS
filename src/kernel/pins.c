#ifdef ARDUINO
#include <avr/io.h>
#endif
#include <stdlib.h>

#include "log.h"
#include "pins.h"

const uint8_t pinsValidArray[PinNB/8] PROGMEM = {
	0xFF, 0xFF, //  0-15
	0xFF, 0x8F, // 16-31
	0x3B, 0xFF, // 32-47
	0x27, 0x7B, // 48-63
	0x00, 0x03, // 64-79
	0xFF, 0xFF, // 80-95
};

uint8_t pinsUsedArray[PinNB/8]={
	0x00, 0x00, //  0-15
	0x00, 0x00, // 16-31
	0x00, 0x00, // 32-47
	0x00, 0x00, // 48-63
	0x00, 0x00, // 64-79
	0x00, 0x00, // 80-95
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
#ifdef ARDUINO
	return (pgm_read_byte_far(pgm_get_far_address(pinIsValid)+pinNum/8)>>(pinNum%8))&1;
#else
	return (pinsValidArray[pinNum/8]>>(pinNum%8))&1;
#endif
}

bool pinGrab(uint8_t pinNum) {
	if (!pinIsValid(pinNum))
		return false;

	if (pinInUse(pinNum))
		return false;

	pinsUsedArray[pinNum/8]|=(1u<<(pinNum%8));

	return true;
}

void pinRelease(uint8_t pinNum) {
	if (!pinIsValid(pinNum))
		return;

	pinsUsedArray[pinNum/8]&=~(1u<<(pinNum%8));
}

bool pinInUse(uint8_t pinNum) {
	if (!pinIsValid(pinNum))
		return false;

	return (pinsUsedArray[pinNum/8]>>(pinNum%8))&1;
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
