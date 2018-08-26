#ifndef KSTR_H
#define KSTR_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#ifdef ARDUINO
#include <avr/pgmspace.h>
#endif

#define KStrTypeNull 0
#define KStrTypeProgmem 1
#define KStrTypeStatic 2
#define KStrTypeHeap 3

typedef struct {
#ifdef ARDUINO
	uint32_t type:8;
	uint32_t ptr:24;
#else
	uint8_t type;
	uint64_t ptr;
#endif
} KStr;

KStr kstrNull(void);
#ifdef ARDUINO
#define kstrAllocProgmem(str) ({static const char _kstrAllocProgmemStr[] PROGMEM = (str); kstrAllocProgmemRaw(pgm_get_far_address(_kstrAllocProgmemStr)); })
KStr kstrAllocProgmemRaw(uint_farptr_t progmemAddr);
#else
#define kstrAllocProgmem(src) kstrAllocCopy(src)
#endif
#define kstrP(s) kstrAllocProgmem(s)
KStr kstrAllocStatic(char *staticBuffer);
#define kstrS(s) kstrAllocStatic(s)
KStr kstrAllocCopy(const char *src);

void kstrFree(KStr *str);

bool kstrIsNull(KStr str);

int16_t kstr_vfprintf(FILE *file, KStr format, va_list ap);

#endif
