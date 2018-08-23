#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

typedef enum {
	LogTypeInfo,
	LogTypeWarning,
	LogTypeError,
} LogType;

#ifdef ARDUINO
// Arduino code to use PROGMEM space for format strings
#include <avr/pgmspace.h>
#define kernelLog(type, format, ...) do { static const char __kernelLogTempStr[128] PROGMEM = (format); kernelLogRaw(type, __kernelLogTempStr, ##__VA_ARGS__); } while(0)
void kernelLogRaw(LogType type, const char *format, ...);
void kernelLogRawV(LogType type, const char *format, va_list ap);
#else
void kernelLog(LogType type, const char *format, ...);
void kernelLogV(LogType type, const char *format, va_list ap);
#endif
const char *logTypeToString(LogType type);

#endif
