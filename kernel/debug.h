#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>

#include "minifs.h"

typedef enum {
	DebugLogTypeInfo,
	DebugLogTypeWarning,
	DebugLogTypeError,
} DebugLogType;

void debugFileIo(void);
void debugFs(void);

bool debugMiniFsAddFile(MiniFs *fs, const char *destPath, const char *srcPath);
bool debugMiniFsAddDir(MiniFs *fs, const char *dirPath);

void debugLog(DebugLogType type, const char *format, ...);
void debugLogV(DebugLogType type, const char *format, va_list ap);
const char *debugLogTypeToString(DebugLogType type);

#endif
