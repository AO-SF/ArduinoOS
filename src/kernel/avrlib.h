#ifdef ARDUINO
#ifndef AVRLIB_H
#define AVRLIB_H

#include <stdint.h>
#include <stdio.h>

int16_t fprintf_PF(FILE *stream, uint_farptr_t format, ...);
int16_t vfprintf_PF(FILE *stream, uint_farptr_t format, va_list ap);

int freeRam(void); // amount of bytes between end of heap and start of stack (technically there may be free blocks inside the heap which can also be used)

#endif
#endif
