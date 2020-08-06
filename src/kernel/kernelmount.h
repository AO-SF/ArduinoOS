#ifndef KERNELMOUNT_H
#define KERNELMOUNT_H

#include <stdbool.h>

#include "kernelfs.h"

typedef enum {
	KernelMountFormatMiniFs,
	KernelMountFormatFlatFile,
	KernelMountFormatPartition1,
	KernelMountFormatPartition2,
	KernelMountFormatPartition3,
	KernelMountFormatPartition4,
	KernelMountFormatCircBuf,
} KernelMountFormat;

bool kernelMount(KernelMountFormat format, const char *devicePath, const char *dirPath);
void kernelUnmount(const char *dirPath);

bool KernelMountFormatIsFile(KernelMountFormat format);
bool KernelMountFormatIsDir(KernelMountFormat format);

#endif
