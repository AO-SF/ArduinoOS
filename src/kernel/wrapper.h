#ifndef ARDUINO

#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>

extern uint32_t kernelBootTime; // Initially set to 0

uint32_t millisRaw(void);
uint32_t millis(void);

#endif
#endif
