#ifndef MINIFSEXTRA_H
#define MINIFSEXTRA_H

#include <stdbool.h>

#include "minifs.h"

bool miniFsExtraAddFile(MiniFs *fs, const char *destPath, const char *srcPath, bool verbose);
bool miniFsExtraAddDir(MiniFs *fs, const char *dirPath, bool verbose);

#endif
