#ifndef BLOCKDEVICEFLATFILE_H
#define BLOCKDEVICEFLATFILE_H

#include "api.h"

BlockDeviceReturnType blockDeviceFlatFileMount(void *fs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData);
BlockDeviceReturnType blockDeviceFlatFileUnmount(void *fs);
BlockDeviceReturnType blockDeviceFlatFileVerify(const void *fs);

BlockDeviceReturnType blockDeviceFlatFileDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
BlockDeviceReturnType blockDeviceFlatFileDirGetChildCount(const void *fs, KStr path, uint16_t *count);
BlockDeviceReturnType blockDeviceFlatFileDirIsEmpty(const void *fs, KStr path, bool *isEmpty);

BlockDeviceReturnType blockDeviceFlatFileDirCreate(void *fs, KStr path);

BlockDeviceReturnType blockDeviceFlatFileFileExists(const void *fs, KStr path, bool *exists);
BlockDeviceReturnType blockDeviceFlatFileFileGetLen(const void *fs, KStr path, uint32_t *size);

BlockDeviceReturnType blockDeviceFlatFileFileResize(void *fs, KStr path, uint32_t newSize);
BlockDeviceReturnType blockDeviceFlatFileFileDelete(void *fs, KStr path);

BlockDeviceReturnType blockDeviceFlatFileFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len, uint16_t *count);
BlockDeviceReturnType blockDeviceFlatFileFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len, uint16_t *count);

#endif
