#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "avrlib.h"
#include "kstr.h"
#include "ktime.h"

size_t kstrOffsetGetOffset(KStr str);
const KStr *kstrOffsetGetSrc(KStr str);

KStr kstrNull(void) {
	KStr kstr={.type=KStrTypeNull, .spare=0, .ptr=(uintptr_t)NULL};
	return kstr;
}

#ifdef ARDUINO
KStr kstrAllocProgmemRaw(uint_farptr_t progmemAddr) {
	KStr kstr={.type=KStrTypeProgmem, .spare=0, .ptr=progmemAddr};
	return kstr;
}
#endif

KStr kstrAllocStatic(char *staticBuffer) {
	KStr kstr={.type=KStrTypeStatic, .spare=0, .ptr=(uintptr_t)staticBuffer};
	return kstr;
}

KStr kstrAllocCopy(const char *src) {
	char *dest=malloc(strlen(src)+1);
	if (dest==NULL)
		return kstrNull();
	strcpy(dest, src);
	KStr kstr={.type=KStrTypeHeap, .spare=0, .ptr=(uintptr_t)dest};
	return kstr;
}

KStr kstrAllocOffset(const KStr *src, size_t offset) {
	if (offset>KStrOffsetMax)
		offset=KStrOffsetMax;

	#ifdef ARDUINO
	// Store 16 bits needed for src pointer into lower part of ptr field
	// Store offset into 8 remaining bits of ptr field
	STATICASSERT(KStrArduinoPtrBits==24);
	STATICASSERT(KStrOffsetBits==8);

	KStr kstr={.type=KStrTypeOffset, .spare=0, .ptr=((((uintptr_t)src) & 0xFFFFu) | (((uintptr_t)(offset & 0xFFu))<<16))};

	assert(kstrOffsetGetOffset(kstr)==offset);
	assert(kstrOffsetGetSrc(kstr)==src);
	#else
	// PC version is simple as we can just use a new field for the offset
	KStr kstr={.type=KStrTypeOffset, .spare=0, .offset=offset, .ptr=(uintptr_t)src};
	#endif

	return kstr;
}

unsigned kstrGetSpare(KStr str) {
	return str.spare;
}

void kstrSetSpare(KStr *str, unsigned spare) {
	if (spare<KStrSpareMax)
		str->spare=spare;
}

char kstrGetChar(KStr str, size_t n) {
	switch(str.type) {
		case KStrTypeNull:
			return 0;
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return pgm_read_byte_far((uint_farptr_t)str.ptr+n);
			#else
			return 0;
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return ((const char *)(uintptr_t)str.ptr)[n];
		break;
		case KStrTypeOffset: {
			size_t offset=kstrOffsetGetOffset(str);
			const KStr *src=kstrOffsetGetSrc(str);
			return kstrGetChar(*src, n+offset);
		} break;
	}
	return 0;
}

uint16_t kstrStrlen(KStr kstr) {
	switch(kstr.type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return strlen_PF((uint_farptr_t)kstr.ptr);
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return strlen((const char *)(uintptr_t)kstr.ptr);
		break;
		case KStrTypeOffset: {
			size_t offset=kstrOffsetGetOffset(kstr);
			const KStr *src=kstrOffsetGetSrc(kstr);
			return kstrStrlen(*src)-offset; // len of underlying str minus our offset
		} break;
	}
	return 0;
}

void kstrStrcpy(char *buf, KStr kstr) {
	// If str is of type KStrTypeOffset then resolve down to base string
	const KStr *str=&kstr;
	size_t offset=0;
	while(str->type==KStrTypeOffset) {
		offset+=kstrOffsetGetOffset(*str);
		str=kstrOffsetGetSrc(*str);
	}

	// Handle base string
	switch(str->type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			strcpy_PF(buf, (uint_farptr_t)(str->ptr+offset));
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			strcpy(buf, (const char *)(uintptr_t)(str->ptr+offset));
		break;
		case KStrTypeOffset:
			// Handled above
			assert(false);
		break;
	}
}

void kstrFree(KStr *str) {
	if (str->type==KStrTypeHeap)
		free((void *)(uintptr_t)str->ptr);
	*str=kstrNull();
}

bool kstrIsNull(KStr str) {
	return (str.type==KStrTypeNull);
}

int kstrStrcmp(const char *a, KStr b) {
	// If b is of type KStrTypeOffset then resolve down to base string
	const KStr *bPtr=&b;
	size_t bOffset=0;
	while(bPtr->type==KStrTypeOffset) {
		bOffset+=kstrOffsetGetOffset(*bPtr);
		bPtr=kstrOffsetGetSrc(*bPtr);
	}

	// Handle base string
	switch(bPtr->type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return strcmp_PF(a, (uint_farptr_t)(bPtr->ptr+bOffset));
			#else
			return 0; // Shouldn't really happen
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return strcmp(a, (const char *)(uintptr_t)(bPtr->ptr+bOffset));
		break;
		case KStrTypeOffset:
			// Handled above
			assert(false);
			return 0;
		break;
	}
	return 0;
}

int kstrDoubleStrcmp(KStr a, KStr b) {
	// Sanity check
	if (kstrIsNull(a) || kstrIsNull(b))
		return 0;

	// Considering the various possible type combinations the simplest method is just to loop comparing bytes
	for(uintptr_t i=0; ; ++i) {
		uint8_t aByte=kstrGetChar(a, i);
		uint8_t bByte=kstrGetChar(b, i);
		if (aByte<bByte)
			return -1;
		else if (aByte>bByte)
			return 1;
		else if (aByte=='\0')
			break;
	}

	return 0;
}

int kstrStrncmp(const char *a, KStr b, size_t n) {
	// If b is of type KStrTypeOffset then resolve down to base string
	const KStr *bPtr=&b;
	size_t bOffset=0;
	while(bPtr->type==KStrTypeOffset) {
		bOffset+=kstrOffsetGetOffset(*bPtr);
		bPtr=kstrOffsetGetSrc(*bPtr);
	}

	// Handled base string
	switch(bPtr->type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return strncmp_PF(a, (uint_farptr_t)(bPtr->ptr+bOffset), n);
			#else
			return 0; // Shouldn't really happen
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return strncmp(a, (const char *)(uintptr_t)(bPtr->ptr+bOffset), n);
		break;
		case KStrTypeOffset:
			// Handled above
			assert(false);
			return 0;
		break;
	}
	return 0;
}

int kstrDoubleStrncmp(KStr a, KStr b, size_t n) {
	// Sanity check
	if (kstrIsNull(a) || kstrIsNull(b))
		return 0;

	// Considering the various possible type combinations the simplest method is just to loop comparing bytes
	for(uintptr_t i=0; i<n; ++i) {
		uint8_t aByte=kstrGetChar(a, i);
		uint8_t bByte=kstrGetChar(b, i);
		if (aByte<bByte)
			return -1;
		else if (aByte>bByte)
			return 1;
		else if (aByte=='\0')
			break;
	}

	return 0;
}

int16_t kstrfprintf(FILE *file, KStr format, ...) {
	va_list ap;
	va_start(ap, format);
	int16_t res=kstrVfprintf(file, format, ap);
	va_end(ap);
	return res;
}

int16_t kstrVfprintf(FILE *file, KStr format, va_list ap) {
	// If format is of type KStrTypeOffset then resolve down to base string
	const KStr *formatPtr=&format;
	size_t offset=0;
	while(formatPtr->type==KStrTypeOffset) {
		offset+=kstrOffsetGetOffset(*formatPtr);
		formatPtr=kstrOffsetGetSrc(*formatPtr);
	}

	// Handle base string
	switch(formatPtr->type) {
		case KStrTypeNull:
			return -1;
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return vfprintf_PF(file, (uint_farptr_t)(formatPtr->ptr+offset), ap);
			#else
			return -1; // Shouldn't really happen
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return vfprintf(file, (const char *)(uintptr_t)(formatPtr->ptr+offset), ap);
		break;
		case KStrTypeOffset:
			// Handled above
			assert(false);
			return -1;
		break;
	}
	return 0;
}

size_t kstrOffsetGetOffset(KStr str) {
	assert(str.type==KStrTypeOffset);

	#ifdef ARDUINO
	STATICASSERT(KStrArduinoPtrBits==24);
	STATICASSERT(KStrOffsetBits==8);
	return (str.ptr>>16) & 0xFFu;
	#else
	return str.offset;
	#endif
}

const KStr *kstrOffsetGetSrc(KStr str) {
	assert(str.type==KStrTypeOffset);

	#ifdef ARDUINO
	return (const KStr *)(str.ptr&0xFFFFu);
	#else
	return (const KStr *)str.ptr;
	#endif
}
