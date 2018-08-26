#include <stdlib.h>
#include <string.h>

#include "kstr.h"
#include "wrapper.h"

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
	if (dest==NULL) {
		KStr kstr={.type=KStrTypeNull, .ptr=(uintptr_t)NULL};
		return kstr;
	}
	strcpy(dest, src);
	KStr kstr={.type=KStrTypeHeap, .ptr=(uintptr_t)dest};
	return kstr;
}

void kstrStrcpy(char *buf, KStr kstr) {
	switch(kstr.type) {
		case KStrTypeNull:
		break;
		case KStrTypeProgmem:
			#ifdef ARDUINO
			return strcpy_PF(buf, (uint_farptr_t)kstr.ptr);
			#else
			return; // Shouldn't really happen
			#endif
		break;
		case KStrTypeStatic:
		case KStrTypeHeap:
			strcpy(buf, (const char *)(uintptr_t)kstr.ptr);
		break;
	}
}

void kstrFree(KStr *str) {
	if (str->type==KStrTypeHeap) {
		free((void *)(uintptr_t)str->ptr);
		str->type=KStrTypeNull;
		str->ptr=(uintptr_t)0;
	}
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
