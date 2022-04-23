#ifndef BLOCKDEVICEMINIFS_H
#define BLOCKDEVICEMINIFS_H

#include "api.h"

BlockDeviceReturnType blockDeviceMiniFsMount(void *fs);
BlockDeviceReturnType blockDeviceMiniFsUnmount(void *fs);
BlockDeviceReturnType blockDeviceMiniFsVerify(const void *fs);

BlockDeviceReturnType blockDeviceMiniFsDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
BlockDeviceReturnType blockDeviceMiniFsDirGetChildCount(const void *fs, KStr path, uint16_t *count);
BlockDeviceReturnType blockDeviceMiniFsDirIsEmpty(const void *fs, KStr path, bool *isEmpty);

BlockDeviceReturnType blockDeviceMiniFsDirCreate(void *fs, KStr path);

BlockDeviceReturnType blockDeviceMiniFsFileExists(const void *fs, KStr path, bool *exists);
BlockDeviceReturnType blockDeviceMiniFsFileGetLen(const void *fs, KStr path, uint32_t *size);

BlockDeviceReturnType blockDeviceMiniFsFileResize(void *fs, KStr path, uint32_t newSize);
BlockDeviceReturnType blockDeviceMiniFsFileDelete(void *fs, KStr path);

BlockDeviceReturnType blockDeviceMiniFsFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len, uint16_t *count);
BlockDeviceReturnType blockDeviceMiniFsFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len, uint16_t *count);

#endif
