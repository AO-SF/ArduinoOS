#ifdef ARDUINO
#include <avr/io.h>
#endif
#include <stdlib.h>

#include "log.h"
#include "pins.h"
#include "util.h"

typedef struct {
	uint8_t valid;
#ifdef ARDUINO
	volatile uint8_t *pin;
	volatile uint8_t *ddr;
	volatile uint8_t *port;
#endif
} PinsGroupData;

PinsGroupData pinsGroupData[PinGroupMax]={
#ifdef ARDUINO
	{.valid=0xFF, .pin=&PINA, .ddr=&DDRA, .port=&PORTA},
	{.valid=0xFF, .pin=&PINB, .ddr=&DDRB, .port=&PORTB},
	{.valid=0xFF, .pin=&PINC, .ddr=&DDRC, .port=&PORTC},
	{.valid=0x8F, .pin=&PIND, .ddr=&DDRD, .port=&PORTD},
	{.valid=0x3B, .pin=&PINE, .ddr=&DDRE, .port=&PORTE},
	{.valid=0xFF, .pin=&PINF, .ddr=&DDRF, .port=&PORTF},
	{.valid=0x27, .pin=&PING, .ddr=&DDRG, .port=&PORTG},
	{.valid=0x7B, .pin=&PINH, .ddr=&DDRH, .port=&PORTH},
	{.valid=0x00, .pin=NULL , .ddr=NULL , .port=NULL  },
	{.valid=0x03, .pin=&PINJ, .ddr=&DDRJ, .port=&PORTJ},
	{.valid=0xFF, .pin=&PINK, .ddr=&DDRK, .port=&PORTK},
	{.valid=0xFF, .pin=&PINL, .ddr=&DDRL, .port=&PORTL},
#else
	{.valid=0xFF},
	{.valid=0xFF},
	{.valid=0xFF},
	{.valid=0x8F},
	{.valid=0x3B},
	{.valid=0xFF},
	{.valid=0x27},
	{.valid=0x7B},
	{.valid=0x00},
	{.valid=0x03},
	{.valid=0xFF},
	{.valid=0xFF},
#endif
};

#ifndef ARDUINO
bool pcWrapperVirtualPinStates[PinMax]={0};
#endif

bool pinIsValid(uint8_t pinNum) {
	if (pinNum>=PinMax)
		return false;
	return (pinsGroupData[PinNumGetGroup(pinNum)].valid>>PinNumGetShift(pinNum))&1;
}

void pinSetMode(uint8_t pinNum, PinMode mode) {
	if (!pinIsValid(pinNum)) {
		kernelLog(LogTypeWarning, kstrP("bad pin %u in setmode\n"), pinNum);
		return;
	}
#ifdef ARDUINO
	switch(mode) {
		case PinModeInput:
			*pinsGroupData[PinNumGetGroup(pinNum)].ddr&=~(1u<<PinNumGetShift(pinNum));
		break;
		case PinModeOutput:
			*pinsGroupData[PinNumGetGroup(pinNum)].ddr|=(1u<<PinNumGetShift(pinNum));
		break;
	}
#else
	// Nothing to do in PC version
#endif
}

bool pinRead(uint8_t pinNum) {
	if (!pinIsValid(pinNum)) {
		kernelLog(LogTypeWarning, kstrP("bad pin %u in read\n"), pinNum);
		return false;
	}
#ifdef ARDUINO
	return ((*pinsGroupData[PinNumGetGroup(pinNum)].pin)&(1u<<PinNumGetShift(pinNum)))!=0;
#else
	return pcWrapperVirtualPinStates[pinNum];
#endif
}

bool pinWrite(uint8_t pinNum, bool value) {
	if (!pinIsValid(pinNum)) {
		kernelLog(LogTypeWarning, kstrP("bad pin %u in write\n"), pinNum);
		return false;
	}
#ifdef ARDUINO
	if (value!=0)
		*pinsGroupData[PinNumGetGroup(pinNum)].port|=(1u<<PinNumGetShift(pinNum));
	else
		*pinsGroupData[PinNumGetGroup(pinNum)].port&=~(1u<<PinNumGetShift(pinNum));
#else
	pcWrapperVirtualPinStates[pinNum]=(value!=0);
#endif
	return true;
}
