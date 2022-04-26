#ifndef BLOCKDEVICEAPI_H
#define BLOCKDEVICEAPI_H

#include <stdbool.h>
#include <stdint.h>

#include "../kstr.h"

typedef uint32_t BlockDeviceFileOffset;
#define BlockDeviceFileOffsetMax UINT32_MAX

#define BlockDevicePathMax 64

typedef enum {
	BlockDeviceReturnTypeSuccess, // operation performed successfully
	BlockDeviceReturnTypeReadError, // could not read from base file
	BlockDeviceReturnTypeCorruptVolume, // inconsistent file system
	BlockDeviceReturnTypeUnsupported, // unsupppored operation/format/option etc
	BlockDeviceReturnTypeFileDoesNotExist,
} BlockDeviceReturnType;

typedef uint32_t (BlockDeviceReadFunctor)(uint32_t addr, uint8_t *data, uint16_t len, void *userData);
typedef uint32_t (BlockDeviceWriteFunctor)(uint32_t addr, const uint8_t *data, uint16_t len, void *userData);

typedef BlockDeviceReturnType (BlockDeviceMount)(void *fs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData);
typedef BlockDeviceReturnType (BlockDeviceUnmount)(void *fs);
typedef BlockDeviceReturnType (BlockDeviceVerify)(const void *fs);

typedef BlockDeviceReturnType (BlockDeviceDirGetChildN)(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
typedef BlockDeviceReturnType (BlockDeviceDirGetChildCount)(const void *fs, KStr path, uint16_t *count);
typedef BlockDeviceReturnType (BlockDeviceDirIsEmpty)(const void *fs, KStr path, bool *isEmpty);

typedef BlockDeviceReturnType (BlockDeviceDirCreate)(void *fs, KStr path);

typedef BlockDeviceReturnType (BlockDeviceFileExists)(const void *fs, KStr path, bool *exists);
typedef BlockDeviceReturnType (BlockDeviceFileGetLen)(const void *fs, KStr path, uint32_t *size);

typedef BlockDeviceReturnType (BlockDeviceFileResize)(void *fs, KStr path, uint32_t newSize);
typedef BlockDeviceReturnType (BlockDeviceFileDelete)(void *fs, KStr path);

typedef BlockDeviceReturnType (BlockDeviceFileRead)(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len, uint16_t *count);
typedef BlockDeviceReturnType (BlockDeviceFileWrite)(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len, uint16_t *count);

typedef struct {
	BlockDeviceMount *mount;
	BlockDeviceUnmount *unmount;
	BlockDeviceVerify *verify;

	BlockDeviceDirGetChildN *dirGetChildN;
	BlockDeviceDirGetChildCount *dirGetChildCount;
	BlockDeviceDirIsEmpty *dirIsEmpty;

	BlockDeviceDirCreate *dirCreate;

	BlockDeviceFileExists *fileExists;
	BlockDeviceFileGetLen *fileGetLen;

	BlockDeviceFileResize *fileResize;
	BlockDeviceFileDelete *fileDelete;

	BlockDeviceFileRead *fileRead;
	BlockDeviceFileWrite *fileWrite;
} BlockDeviceFunctors;

#endif
