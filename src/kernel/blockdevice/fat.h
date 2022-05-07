#ifndef BLOCKDEVICEFAT_H
#define BLOCKDEVICEFAT_H

#include "api.h"

uint16_t blockDeviceFatStructSize(void);

uint32_t blockDeviceFatGetLastResult(const void *fs);

BlockDeviceReturnType blockDeviceFatMount(void *fs, void *userData);
BlockDeviceReturnType blockDeviceFatUnmount(void *fs);
BlockDeviceReturnType blockDeviceFatVerify(const void *fs);

BlockDeviceReturnType blockDeviceFatDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
BlockDeviceReturnType blockDeviceFatDirGetChildCount(const void *fs, KStr path);
BlockDeviceReturnType blockDeviceFatDirIsEmpty(const void *fs, KStr path);

BlockDeviceReturnType blockDeviceFatDirCreate(void *fs, KStr path);

BlockDeviceReturnType blockDeviceFatFileExists(const void *fs, KStr path);
BlockDeviceReturnType blockDeviceFatFileGetLen(const void *fs, KStr path);

BlockDeviceReturnType blockDeviceFatFileResize(void *fs, KStr path, uint32_t newSize);
BlockDeviceReturnType blockDeviceFatFileDelete(void *fs, KStr path);

BlockDeviceReturnType blockDeviceFatFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len);
BlockDeviceReturnType blockDeviceFatFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len);

#endif
