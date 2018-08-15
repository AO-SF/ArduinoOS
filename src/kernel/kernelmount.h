#ifndef KERNELMOUNT_H
#define KERNELMOUNT_H

#include <stdbool.h>

#include "kernelfs.h"

bool kernelMount(KernelFsBlockDeviceFormat format, const char *devicePath, const char *dirPath);
void kernelUnmount(const char *devicePath);

#endif
