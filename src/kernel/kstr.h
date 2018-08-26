#ifndef KSTR_H
#define KSTR_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#ifdef ARDUINO
#include <avr/pgmspace.h>
#endif

#define KStrTypeInvalid 0
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

#ifdef ARDUINO
#define kstrAllocProgmem(str) ({static const char _kstrAllocProgmemStr[] PROGMEM = (str); kstrAllocProgmemRaw(pgm_get_far_address(_kstrAllocProgmemStr)); })
KStr kstrAllocProgmemRaw(uint_farptr_t progmemAddr);
#else
#define kstrAllocProgmem(src) kstrAllocCopy(src)
#endif
#define kstrP(s) kstrAllocProgmem(s)
KStr kstrAllocStatic(char *staticBuffer);
KStr kstrAllocCopy(const char *src);

void kstrFree(KStr *str);

#endif
