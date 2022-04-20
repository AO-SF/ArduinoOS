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

#define KStrTypeNull 0 // equiavlent to a NULL pointer for normal strings
#define KStrTypeProgmem 1 // string buffer is stored in read-only progmem when running on Arduino (simply uses heap on PC)
#define KStrTypeStatic 2 // string is stored using a static buffer provided
#define KStrTypeHeap 3 // string is stored using dynamic memory
#define KStrTypeOffset 4 // points into another KStr - potentially with an offset. source string must be kept around until all offset strings are finished with
#define KStrTypeBits 3

#define KStrSpareBits 5
#define KStrSpareMax ((1u)<<KStrSpareBits)
#define KStrArduinoPtrBits 24

#define KStrOffsetBits (KStrArduinoPtrBits-16) // as we need 16 bits to hold a 'normal' pointer while running on the Arduino
#define KStrOffsetMax ((1u)<<KStrOffsetBits)

STATICASSERT(KStrTypeBits+KStrSpareBits==8);
STATICASSERT(KStrTypeBits+KStrSpareBits+KStrArduinoPtrBits==32);

typedef struct {
#ifdef ARDUINO
	uint32_t type:KStrTypeBits;
	uint32_t spare:KStrSpareBits;
	uint32_t ptr:KStrArduinoPtrBits;
	// note: if type is KStrTypeOffset then we know the src pointer only needs 16 bits and so we can reuse the upper 8 bits to store the offset
#else
	uint8_t type:KStrTypeBits;
	uint8_t spare:KStrSpareBits;
	uint8_t offset; // used only for KStrTypeOffset
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
KStr kstrAllocOffset(const KStr *src, size_t offset); // offset should not exceed KStrOffsetMax

#define kstrP(s) kstrAllocProgmem(s)
#define kstrS(s) kstrAllocStatic(s)
#define kstrC(s) kstrAllocCopy(s)
#define kstrO(s,o) kstrAllocOffset(s, o)

// These functions provide access to the spare bits in the KStr struct.
unsigned kstrGetSpare(KStr str);
void kstrSetSpare(KStr *str, unsigned spare); // [0, KStrSpareMax-1], which with KStrSpareBits=6 this is 63

char kstrGetChar(KStr str, size_t n); // n must be less than string's length

uint16_t kstrStrlen(KStr kstr);

void kstrStrcpy(char *buf, KStr kstr);

void kstrFree(KStr *str);

bool kstrIsNull(KStr str);

int kstrStrcmp(const char *a, KStr b);
int kstrDoubleStrcmp(KStr a, KStr b);

int kstrStrncmp(const char *a, KStr b, size_t n);
int kstrDoubleStrncmp(KStr a, KStr b, size_t n);

int16_t kstrfprintf(FILE *file, KStr format, ...);
int16_t kstrVfprintf(FILE *file, KStr format, va_list ap);

#endif
