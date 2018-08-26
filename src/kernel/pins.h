#ifdef ARDUINO

#ifndef PINS_H
#define PINS_H

#include <stdbool.h>
#include <stdint.h>

#define PinD0 0
#define PinD1 1
#define PinD2 2
#define PinD3 3
#define PinD4 4
#define PinD5 5
#define PinD6 6
#define PinD7 7
#define PinD8 8
#define PinD9 9
#define PinD10 10
#define PinD11 11
#define PinD12 12
#define PinD13 13
#define PinD14 14
#define PinD15 15
#define PinD16 16
#define PinD17 17
#define PinD18 18
#define PinD19 19
#define PinD20 20
#define PinD21 21
#define PinD22 22
#define PinD23 23
#define PinD24 24
#define PinD25 25
#define PinD26 26
#define PinD27 27
#define PinD28 28
#define PinD29 29
#define PinD30 30
#define PinD31 31
#define PinD32 32
#define PinD33 33
#define PinD34 34
#define PinD35 35
#define PinD36 36
#define PinD37 37
#define PinD38 38
#define PinD39 39
#define PinD40 40
#define PinD41 41
#define PinD42 42
#define PinD43 43
#define PinD44 44
#define PinD45 45
#define PinD46 46
#define PinD47 47
#define PinD48 48
#define PinD49 49
#define PinD50 50
#define PinD51 51
#define PinD52 52
#define PinD53 53

#define PinA0 55
#define PinA1 56
#define PinA2 57
#define PinA3 58
#define PinA4 59
#define PinA5 60
#define PinA6 61
#define PinA7 62
#define PinA8 63
#define PinA9 64
#define PinA10 65
#define PinA11 66
#define PinA12 67
#define PinA13 68
#define PinA14 69
#define PinA15 70

bool pinRead(uint8_t pinNum);
void pinWrite(uint8_t pinNum, bool value);

#endif
#endif
