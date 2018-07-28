#ifndef DEBUG_H
#define DEBUG_H

#include "minifs.h"

void debugFileIo(void);
void debugFs(void);

bool debugMiniFsAddFile(MiniFs *fs, const char *destPath, const char *srcPath);
bool debugMiniFsAddDir(MiniFs *fs, const char *dirPath);

#endif
