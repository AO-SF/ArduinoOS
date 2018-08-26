#ifndef KSTR_H
#define KSTR_H

#include <stdint.h>
#ifdef ARDUINO
#include <avr/pgmspace.h>
#endif

#define KStrTypeInvalid 0
#define KStrTypeProgmem 1
#define KStrTypeStatic 2
#define KStrTypeHeap 3

typedef struct {
	uint32_t type:8;
	uint32_t ptr:24;
} KStr;

#ifdef ARDUINO
#define kstrAllocProgmem(str) ({static const char _kstrAllocProgmemStr[] PROGMEM = (str); kstrAllocProgmemRaw(_kstrAllocProgmemStr); })
KStr kstrAllocProgmemRaw(uint_farptr_t progmemAddr);
#else
#define kstrAllocProgmemRaw(src) kstrallocCopy(src);
#endif
KStr kstrAllocStatic(char *staticBuffer);
KStr kstrAllocCopy(const char *src);

void kstrFree(KStr *str);

#endif
