#ifndef WRAPPER_H
#define WRAPPER_H

#ifdef ARDUINO
#include <stdbool.h>
extern void *pointerIsHeapBase; // should be set on startup
#endif
#include <stdint.h>

#define _unused(x) ((void)(x))

extern uint32_t kernelBootTime; // Initially set to 0

uint32_t millisRaw(void);
uint32_t millis(void);

void delay(uint32_t ms);

#ifdef ARDUINO
bool pointerIsHeap(const void *ptr);
#endif

#endif
