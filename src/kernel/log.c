#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#else
#include <sys/types.h>
#endif

#include "avrlib.h"
#include "log.h"
#include "ktime.h"
#include "util.h"

LogLevel kernelLogLevel=LogLevelInfo;

void kernelLog(LogType type, KStr format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelLogV(type, format, ap);
	va_end(ap);
}

void kernelLogV(LogType type, KStr format, va_list ap) {
	// No need to print this type at the current logging level?
	if ((LogLevel)type<kernelLogGetLevel()) {
		kstrFree(&format);
		return;
	}

	// Open file if needed
	#ifdef ARDUINO
	FILE *file=stdout;
	#else
	FILE *file=fopen("kernel.log", "a");
	#endif

	if (file!=NULL) {
		// Print time
		uint64_t t=ktimeGetMonotonicMs();
		uint64_t d=t/(24llu*60llu*60llu*1000llu);
		t-=d*(24llu*60llu*60llu*1000llu);
		uint16_t h=t/(60llu*60llu*1000llu);
		t-=h*(60llu*60llu*1000llu);
		uint16_t m=t/(60llu*1000llu);
		t-=m*(60llu*1000llu);
		uint16_t s=t/1000llu;
		uint16_t ms=t-s*1000llu;
		fprintf(file, "%4"PRIu64":%02u:%02u:%02u:%03u ", d, h, m, s, ms);

		// Print free memory (arduino only)
		#ifdef ARDUINO
		fprintf(file, "fr%04u ", freeRam());
		#endif

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

void kernelLogAppend(LogType type, KStr format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelLogAppendV(type, format, ap);
	va_end(ap);
}

void kernelLogAppendV(LogType type, KStr format, va_list ap) {
	// No need to print this type at the current logging level?
	if ((LogLevel)type<kernelLogGetLevel()) {
		kstrFree(&format);
		return;
	}

	// Open file if needed
	#ifdef ARDUINO
	FILE *file=stdout;
	#else
	FILE *file=fopen("kernel.log", "a");
	#endif

	if (file!=NULL) {
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

LogLevel kernelLogGetLevel(void) {
	return kernelLogLevel;
}

void kernelLogSetLevel(LogLevel level) {
	kernelLogLevel=level;
}
