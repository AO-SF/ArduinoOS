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
	KernelMountFormatFat,
} KernelMountFormat;

#define kernelRemountCopyBufferSize 256 // something lower such as 16-64 would suffice, but we have a spare 256 byte buffer in procman so this makes sense for now

bool kernelMount(KernelMountFormat format, const char *devicePath, const char *dirPath);
void kernelUnmount(const char *dirPath);
bool kernelRemount(KernelMountFormat newFormat, const char *newDevicePath, const char *dirPath);
bool kernelRemountWithBuffers(KernelMountFormat newFormat, const char *newDevicePath, const char *dirPath, char *pathBuffer, uint8_t *copyBuffer); // like kernelRemount function but uses provided buffers to do the copying etc. The pathBuffer and copyBuffer must be able to contain at least KernelFsPathMax and kernelRemountBufferSize bytes, respectively.

bool KernelMountFormatIsFile(KernelMountFormat format);
bool KernelMountFormatIsDir(KernelMountFormat format);

#endif
