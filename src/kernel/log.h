#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

typedef enum {
	LogTypeInfo,
	LogTypeWarning,
	LogTypeError,
} LogType;

void kernelLog(LogType type, const char *format, ...);
void kernelLogV(LogType type, const char *format, va_list ap);
const char *logTypeToString(LogType type);

#endif
