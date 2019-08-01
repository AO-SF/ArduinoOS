#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

#include "kstr.h"

typedef enum {
	LogTypeInfo,
	LogTypeWarning,
	LogTypeError,
} LogType;

typedef enum {
	LogLevelInfo, // error+warning+info
	LogLevelWarning, // error+warning
	LogLevelError, // error
	LogLevelNone,
} LogLevel;

void kernelLog(LogType type, KStr format, ...);
void kernelLogV(LogType type, KStr format, va_list ap);
const char *logTypeToString(LogType type);

LogLevel kernelLogGetLevel(void);
void kernelLogSetLevel(LogLevel level);

#endif
