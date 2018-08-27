#ifdef ARDUINO
#ifndef AVRLIB_H
#define AVRLIB_H

#include <stdint.h>
#include <stdio.h>

int16_t fprintf_PF(FILE *stream, uint_farptr_t format, ...);
int16_t vfprintf_PF(FILE *stream, uint_farptr_t format, va_list ap);

#endif
#endif
