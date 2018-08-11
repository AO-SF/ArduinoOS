#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>

typedef enum {
	DebugLogTypeInfo,
	DebugLogTypeWarning,
	DebugLogTypeError,
} DebugLogType;

void debugFileIo(void);
void debugFs(void);

void debugLog(DebugLogType type, const char *format, ...);
void debugLogV(DebugLogType type, const char *format, va_list ap);
const char *debugLogTypeToString(DebugLogType type);

#endif
