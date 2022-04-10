#ifndef KSTR_H
#define KSTR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifdef ARDUINO
#include <avr/pgmspace.h>
#endif

#include "util.h"

#define KStrTypeNull 0
#define KStrTypeProgmem 1
#define KStrTypeStatic 2
#define KStrTypeHeap 3
#define KStrTypeBits 2

#define KStrSpareBits 6
#define KStrSpareMax ((1u)<<KStrSpareBits)
#define KStrArduinoPtrBits 24

STATICASSERT(KStrTypeBits+KStrSpareBits==8);
STATICASSERT(KStrTypeBits+KStrSpareBits+KStrArduinoPtrBits==32);

typedef struct {
#ifdef ARDUINO
	uint32_t type:KStrTypeBits;
	uint32_t spare:KStrSpareBits;
	uint32_t ptr:KStrArduinoPtrBits;
#else
	uint8_t type:KStrTypeBits;
	uint8_t spare:KStrSpareBits;
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
KStr kstrAllocStatic(char *staticBuffer);
KStr kstrAllocCopy(const char *src);

#define kstrP(s) kstrAllocProgmem(s)
#define kstrS(s) kstrAllocStatic(s)
#define kstrC(s) kstrAllocCopy(s)

// These functions provide access to the spare bits in the KStr struct.
unsigned kstrGetSpare(KStr str);
void kstrSetSpare(KStr *str, unsigned spare); // [0, KStrSpareMax-1], which with KStrSpareBits=6 this is 63

uint16_t kstrStrlen(KStr kstr);

void kstrStrcpy(char *buf, KStr kstr);

void kstrFree(KStr *str);

bool kstrIsNull(KStr str);

int kstrStrcmp(const char *a, KStr b);
int kstrDoubleStrcmp(KStr a, KStr b);

int kstrStrncmp(const char *a, KStr b, size_t n);
int kstrDoubleStrncmp(KStr a, KStr b, size_t n);

// the following functions return the amount of bytes at the start of a and b which match (does not include null terminators)
unsigned kstrMatchLen(const char *a, KStr b);
unsigned kstrDoubleMatchLen(KStr a, KStr b);

int16_t kstrVfprintf(FILE *file, KStr format, va_list ap);

#endif
