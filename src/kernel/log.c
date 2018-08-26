#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#else
#include <sys/types.h>
#endif

#include "log.h"
#include "wrapper.h"

void kernelLog(LogType type, KStr format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelLogV(type, format, ap);
	va_end(ap);
}

void kernelLogV(LogType type, KStr format, va_list ap) {
	// Open file if needed
	#ifdef ARDUINO
	FILE *file=stdout;
	#else
	FILE *file=fopen("kernel.log", "a");
	#endif

	if (file!=NULL) {
		// Print time
		uint32_t t=millis();
		unsigned h=t/(60u*60u*1000u);
		t-=h*(60u*60u*1000u);
		unsigned m=t/(60u*1000u);
		t-=m*(60u*1000u);
		unsigned s=t/1000u;
		unsigned ms=t-s*1000u;
		fprintf(file, "%3u:%02u:%02u:%03u ", h, m, s, ms);

		// Print log type
		fprintf(file, "%7s ", logTypeToString(type));

		// Print user string
		kstrVfprintf(file, format, ap);

		// Close file if needed
		#ifndef ARDUINO
		fclose(file);
		#endif
	}

	kstrFree(&format);
}

static const char *logTypeToStringArray[]={
	[LogTypeInfo]="INFO",
	[LogTypeWarning]="WARNING",
	[LogTypeError]="ERROR",
};
const char *logTypeToString(LogType type) {
	return logTypeToStringArray[type];
}
