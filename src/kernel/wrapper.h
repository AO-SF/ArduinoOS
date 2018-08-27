#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>

#define _unused(x) ((void)(x))

extern uint32_t kernelBootTime; // Initially set to 0

void millisInit(void);
uint32_t millisRaw(void);
uint32_t millis(void);

void delay(uint32_t ms);

#endif
