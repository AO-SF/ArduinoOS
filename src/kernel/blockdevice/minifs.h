#ifndef BLOCKDEVICEMINIFS_H
#define BLOCKDEVICEMINIFS_H

#include "api.h"

uint16_t blockDeviceMiniFsStructSize(void);

uint32_t blockDeviceMiniFsGetLastResult(const void *fs);

BlockDeviceReturnType blockDeviceMiniFsMount(void *fs, void *userData);
BlockDeviceReturnType blockDeviceMiniFsUnmount(void *fs);
BlockDeviceReturnType blockDeviceMiniFsVerify(const void *fs);

BlockDeviceReturnType blockDeviceMiniFsDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
BlockDeviceReturnType blockDeviceMiniFsDirGetChildCount(const void *fs, KStr path);
BlockDeviceReturnType blockDeviceMiniFsDirIsEmpty(const void *fs, KStr path);

BlockDeviceReturnType blockDeviceMiniFsDirCreate(void *fs, KStr path);

BlockDeviceReturnType blockDeviceMiniFsFileExists(const void *fs, KStr path);
BlockDeviceReturnType blockDeviceMiniFsFileGetLen(void *fs, KStr path);

BlockDeviceReturnType blockDeviceMiniFsFileResize(void *fs, KStr path, uint32_t newSize);
BlockDeviceReturnType blockDeviceMiniFsFileDelete(void *fs, KStr path);

BlockDeviceReturnType blockDeviceMiniFsFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len);
BlockDeviceReturnType blockDeviceMiniFsFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len);

#endif
