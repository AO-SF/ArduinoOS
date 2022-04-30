#ifndef BLOCKDEVICEAPI_H
#define BLOCKDEVICEAPI_H

#include <stdbool.h>
#include <stdint.h>

#include "../kstr.h"

typedef uint32_t BlockDeviceFileOffset;
#define BlockDeviceFileOffsetMax UINT32_MAX

#define BlockDevicePathMax 64

typedef uint32_t BlockDeviceReturnType;
#define BlockDeviceReturnTypeIOError ((uint32_t)((int32_t)-1)) // could not read/write base file/disk
#define BlockDeviceReturnTypeCorruptVolume ((uint32_t)((int32_t)-2)) // inconsistent file system
#define BlockDeviceReturnTypeUnsupported ((uint32_t)((int32_t)-3)) // unsuppported operation/format/option etc
#define BlockDeviceReturnTypeFileDoesNotExist ((uint32_t)((int32_t)-4)) // no such file (e.g. BlockDeviceFileGetLen when path does not point to a file)
#define BlockDeviceReturnTypeSuccess ((uint32_t)((int32_t)-5)) // operation performed successfully
#define blockDeviceReturnTypeIsSuccess(r) ((r)<=BlockDeviceReturnTypeSuccess)

typedef uint32_t (BlockDeviceReadFunctor)(uint32_t addr, uint8_t *data, uint16_t len, void *userData);
typedef uint32_t (BlockDeviceWriteFunctor)(uint32_t addr, const uint8_t *data, uint16_t len, void *userData);

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceMount)(void *fs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData);
typedef BlockDeviceReturnType (BlockDeviceUnmount)(void *fs);
typedef BlockDeviceReturnType (BlockDeviceVerify)(const void *fs);

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceDirGetChildN)(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]);
typedef BlockDeviceReturnType (BlockDeviceDirGetChildCount)(const void *fs, KStr path); // on success returns child count
typedef BlockDeviceReturnType (BlockDeviceDirIsEmpty)(const void *fs, KStr path); // on success returns 1/0 for true/false

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceDirCreate)(void *fs, KStr path);

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceFileExists)(const void *fs, KStr path); // on success returns 1/0 for true/false (does not return BlockDeviceReturnTypeFileDoesNotExist as this is an error value)
typedef BlockDeviceReturnType (BlockDeviceFileGetLen)(const void *fs, KStr path); // on success returns length

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceFileResize)(void *fs, KStr path, uint32_t newSize);
typedef BlockDeviceReturnType (BlockDeviceFileDelete)(void *fs, KStr path);

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
// On success they return number of bytes read/written.
typedef BlockDeviceReturnType (BlockDeviceFileRead)(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len);
typedef BlockDeviceReturnType (BlockDeviceFileWrite)(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len);

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
