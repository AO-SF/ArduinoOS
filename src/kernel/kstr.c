#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "avrlib.h"
#include "kstr.h"
#include "ktime.h"

KStr kstrNull(void) {
	KStr kstr={.type=KStrTypeNull, .ptr=(uintptr_t)NULL};
	return kstr;
}

#ifdef ARDUINO
KStr kstrAllocProgmemRaw(uint_farptr_t progmemAddr) {
	KStr kstr={.type=KStrTypeProgmem, .ptr=progmemAddr};
	return kstr;
}
#endif

KStr kstrAllocStatic(char *staticBuffer) {
	KStr kstr={.type=KStrTypeStatic, .ptr=(uintptr_t)staticBuffer};
	return kstr;
}

KStr kstrAllocCopy(const char *src) {
	char *dest=malloc(strlen(src)+1);
	if (dest==NULL)
		return kstrNull();
	strcpy(dest, src);
	KStr kstr={.type=KStrTypeHeap, .ptr=(uintptr_t)dest};
	return kstr;
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
	}
	return 0;
}

void kstrStrcpy(char *buf, KStr kstr) {
	switch(kstr.type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			strcpy_PF(buf, (uint_farptr_t)kstr.ptr);
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			strcpy(buf, (const char *)(uintptr_t)kstr.ptr);
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
	switch(b.type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return strcmp_PF(a, (uint_farptr_t)b.ptr);
			#else
			return 0; // Shouldn't really happen
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return strcmp(a, (const char *)(uintptr_t)b.ptr);
		break;
	}
	return 0;
}

int kstrDoubleStrcmp(KStr a, KStr b) {
	// Simply cases
	if (a.type==KStrTypeNull || b.type==KStrTypeNull)
		return 0;
	if (a.type==KStrTypeStatic || a.type==KStrTypeHeap)
		return kstrStrcmp((const char *)(uintptr_t)a.ptr, b);
	if (b.type==KStrTypeStatic || b.type==KStrTypeHeap)
		return kstrStrcmp((const char *)(uintptr_t)b.ptr, a);

	// Otherwise both are in progmem
#ifdef ARDUINO
	assert(a.type==KStrTypeProgmem && b.type==KStrTypeProgmem);
	for(uintptr_t i=0; ; ++i) {
		uint8_t aByte=pgm_read_byte_far(a.ptr+i);
		uint8_t bByte=pgm_read_byte_far(b.ptr+i);
		if (aByte<bByte)
			return -1;
		else if (aByte>bByte)
			return 1;
		else if (aByte=='\0')
			break;
	}
#endif
	return 0;
}

int16_t kstrVfprintf(FILE *file, KStr format, va_list ap) {
	switch(format.type) {
		case KStrTypeNull:
			return -1;
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return vfprintf_PF(file, (uint_farptr_t)format.ptr, ap);
			#else
			return -1; // Shouldn't really happen
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			return vfprintf(file, (const char *)(uintptr_t)format.ptr, ap);
		break;
	}
	return 0;
}
