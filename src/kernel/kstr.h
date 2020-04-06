#ifndef KSTR_H
#define KSTR_H

#include <stdarg.h>
#include <stdbool.h>
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
KStr kstrAllocStatic(char *staticBuffer);
KStr kstrAllocCopy(const char *src);

#define kstrP(s) kstrAllocProgmem(s)
#define kstrS(s) kstrAllocStatic(s)
#define kstrC(s) kstrAllocCopy(s)

uint16_t kstrStrlen(KStr kstr);

void kstrStrcpy(char *buf, KStr kstr);

void kstrFree(KStr *str);

bool kstrIsNull(KStr str);

int kstrStrcmp(const char *a, KStr b);
int kstrDoubleStrcmp(KStr a, KStr b);

int kstrStrncmp(const char *a, KStr b, size_t n);
int kstrDoubleStrncmp(KStr a, KStr b, size_t n);

int16_t kstrVfprintf(FILE *file, KStr format, va_list ap);

#endif
