#ifndef PINS_H
#define PINS_H

#include <stdbool.h>
#include <stdint.h>

#define PinNB 96 // all valid pin numbers are less than this value

#define PinInvalid 127

#define PinA0 40
#define PinA1 41
#define PinA2 42
#define PinA3 43
#define PinA4 44
#define PinA5 45
#define PinA6 46
#define PinA7 47
#define PinA8 80
#define PinA9 81
#define PinA10 82
#define PinA11 83
#define PinA12 84
#define PinA13 85
#define PinA14 86
#define PinA15 87

#define PinD0 32
#define PinD1 33
#define PinD2 36
#define PinD3 37
#define PinD4 53
#define PinD5 35
#define PinD6 59
#define PinD7 60
#define PinD8 61
#define PinD9 62
#define PinD10 12
#define PinD11 13
#define PinD12 14
#define PinD13 15
#define PinD14 73
#define PinD15 72
#define PinD16 57
#define PinD17 56
#define PinD18 27
#define PinD19 26
#define PinD20 25
#define PinD21 24
#define PinD22 0
#define PinD23 1
#define PinD24 2
#define PinD25 3
#define PinD26 4
#define PinD27 5
#define PinD28 6
#define PinD29 7
#define PinD30 23
#define PinD31 22
#define PinD32 21
#define PinD33 20
#define PinD34 19
#define PinD35 18
#define PinD36 17
#define PinD37 16
#define PinD38 31
#define PinD39 50
#define PinD40 49
#define PinD41 48
#define PinD42 95
#define PinD43 94
#define PinD44 93
#define PinD45 92
#define PinD46 91
#define PinD47 90
#define PinD48 89
#define PinD49 88
#define PinD50 11
#define PinD51 10
#define PinD52 9
#define PinD53 8

#define PinLed PinD13

typedef enum {
	PinModeInput=0,
	PinModeOutput=1,
} PinMode;

bool pinIsValid(uint8_t pinNum);

bool pinGrab(uint8_t pinNum); // returns false if already reserved
void pinRelease(uint8_t pinNum);
bool pinInUse(uint8_t pinNum);

void pinSetMode(uint8_t pinNum, PinMode mode);
bool pinRead(uint8_t pinNum);
bool pinWrite(uint8_t pinNum, bool value);

void pinsDebug(void);

#endif
