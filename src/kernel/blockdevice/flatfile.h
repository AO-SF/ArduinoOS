#ifndef BLOCKDEVICEFLATFILE_H
#define BLOCKDEVICEFLATFILE_H

#include "api.h"

uint16_t blockDeviceFlatFileStructSize(void);

BlockDeviceReturnType blockDeviceFlatFileMount(void *fs, void *userData);
BlockDeviceReturnType blockDeviceFlatFileUnmount(void *fs);
BlockDeviceReturnType blockDeviceFlatFileVerify(const void *fs);

BlockDeviceReturnType blockDeviceFlatFileDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
BlockDeviceReturnType blockDeviceFlatFileDirGetChildCount(const void *fs, KStr path);
BlockDeviceReturnType blockDeviceFlatFileDirIsEmpty(const void *fs, KStr path);

BlockDeviceReturnType blockDeviceFlatFileDirCreate(void *fs, KStr path);

BlockDeviceReturnType blockDeviceFlatFileFileExists(const void *fs, KStr path);
BlockDeviceReturnType blockDeviceFlatFileFileGetLen(const void *fs, KStr path);

BlockDeviceReturnType blockDeviceFlatFileFileResize(void *fs, KStr path, uint32_t newSize);
BlockDeviceReturnType blockDeviceFlatFileFileDelete(void *fs, KStr path);

BlockDeviceReturnType blockDeviceFlatFileFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len);
BlockDeviceReturnType blockDeviceFlatFileFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len);

#endif
