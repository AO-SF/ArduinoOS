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

#ifdef ARDUINO
void kernelLogRaw(LogType type, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelLogRawV(type, format, ap);
	va_end(ap);
}

void kernelLogRawV(LogType type, const char *format, va_list ap) {
	// Print time
	uint32_t t=millis();
	uint32_t h=t/(60u*60u*1000u);
	t-=h*(60u*60u*1000u);
	uint32_t m=t/(60u*1000u);
	t-=m*(60u*1000u);
	uint32_t s=t/1000u;
	uint32_t ms=t-s*1000u;
	printf("%3lu:%02lu:%02lu:%03lu ", h, m, s, ms);

	// Print log type
	printf("%7s ", logTypeToString(type));

	// Print user string
	// TODO: Use vfprintf_PF if it existed (and update format type to uint_ptr_far or w/e), otherwise consider rolling our own PF version. For now this seems to work anyway as log strings are put early (by chance).
	vfprintf_P(stdout, format, ap);
}
#else
void kernelLog(LogType type, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelLogV(type, format, ap);
	va_end(ap);
}

void kernelLogV(LogType type, const char *format, va_list ap) {
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
}
#endif

const char *logTypeToString(LogType type) {
	static const char *logTypeToStringArray[]={
		[LogTypeInfo]="INFO",
		[LogTypeWarning]="WARNING",
		[LogTypeError]="ERROR",
	};
	return logTypeToStringArray[type];
}
