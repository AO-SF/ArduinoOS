#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

#include "kstr.h"

typedef enum {
	LogTypeInfo,
	LogTypeWarning,
	LogTypeError,
} LogType;

void kernelLog(LogType type, KStr format, ...);
void kernelLogV(LogType type, KStr format, va_list ap);
const char *logTypeToString(LogType type);

#endif
