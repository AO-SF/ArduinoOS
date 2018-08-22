#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#else
#include <sys/types.h>
#endif

#include "log.h"
#include "wrapper.h"

void kernelLog(LogType type, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelLogV(type, format, ap);
	va_end(ap);
}

void kernelLogV(LogType type, const char *format, va_list ap) {
	// TODO: Think about Arduino case
#ifndef ARDUINO
	// Open file
	FILE *file=fopen("kernel.log", "a");
	if (file!=NULL) {
		// Print time
		uint32_t t=millis();
		uint32_t h=t/(60u*60u*1000u);
		t-=h*(60u*60u*1000u);
		uint32_t m=t/(60u*1000u);
		t-=m*(60u*1000u);
		uint32_t s=t/1000u;
		uint32_t ms=t-s*1000u;
		fprintf(file, "%3u:%02u:%02u:%03u ", h, m, s, ms);

		// Print log type
		fprintf(file, "%7s ", logTypeToString(type));

		// Print user string
		vfprintf(file, format, ap);

		// Close file
		fclose(file);
	}
#endif
}

const char *logTypeToString(LogType type) {
	static const char *logTypeToStringArray[]={
		[LogTypeInfo]="INFO",
		[LogTypeWarning]="WARNING",
		[LogTypeError]="ERROR",
	};
	return logTypeToStringArray[type];
}
