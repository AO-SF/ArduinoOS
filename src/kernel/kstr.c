#include <stdlib.h>
#include <string.h>

#include "kstr.h"

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
		KStr kstr={.type=KStrTypeInvalid, .ptr=(uintptr_t)NULL};
		return kstr;
	}
	strcpy(dest, src);
	KStr kstr={.type=KStrTypeHeap, .ptr=(uintptr_t)dest};
	return kstr;
}

void kstrFree(KStr *str) {
	if (str->type==KStrTypeHeap) {
		free((void *)(uintptr_t)str->ptr);
		str->type=KStrTypeInvalid;
		str->ptr=(uintptr_t)0;
	}
}
