#include <stdlib.h>
#include <string.h>

#include "kstr.h"
#include "wrapper.h"

KStr kstrallocStatic(char *staticBuffer) {
#ifdef ARDUINO
	return staticBuffer;
#else
	// On PC just use malloc - we have plenty of space, and saves kstrfree having to determine if heap or not
	return kstrallocCopy(staticBuffer);
#endif
}

KStr kstrallocCopy(const char *src) {
	char *dest=malloc(strlen(src)+1);
	if (dest==NULL)
		return NULL;
	strcpy(dest, src);
	return dest;
}

void kstrfree(KStr str) {
#ifdef ARDUINO
	if (pointerIsHeap(str))
		free(str);
#else
	free(str);
#endif
}

char *kstrGetC(KStr str) {
	return (char *)str;
}
