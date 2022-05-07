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
#define BlockDeviceReturnTypeOverflow ((uint32_t)((int32_t)-5)) // result of operation is too large to return (i.e. because it equals one of these reserved return types), call getLastResult function immediately after to find true result
#define BlockDeviceReturnTypeSuccess ((uint32_t)((int32_t)-6)) // operation performed successfully
#define blockDeviceReturnTypeIsSuccess(r) ((r)<=BlockDeviceReturnTypeSuccess)

// Returns the size needed for the fs instance each function uses
typedef uint16_t (BlockDeviceStructSize)(void);

// If a function returns BlockDeviceReturnTypeOverflow, calling this immediately after should return the true value
typedef uint32_t (BlockDeviceGetLastResult)(const void *fs);

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceMount)(void *fs, void *userData);
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
typedef BlockDeviceReturnType (BlockDeviceFileGetLen)(void *fs, KStr path); // on success returns length but can also return BlockDeviceReturnTypeOverflow

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
typedef BlockDeviceReturnType (BlockDeviceFileResize)(void *fs, KStr path, uint32_t newSize);
typedef BlockDeviceReturnType (BlockDeviceFileDelete)(void *fs, KStr path);

// These can return: BlockDeviceReturnTypeIOError, BlockDeviceReturnTypeCorruptVolume, BlockDeviceReturnTypeUnsupported, BlockDeviceReturnTypeFileDoesNotExist, BlockDeviceReturnTypeSuccess
// On success they return number of bytes read/written.
typedef BlockDeviceReturnType (BlockDeviceFileRead)(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len);
typedef BlockDeviceReturnType (BlockDeviceFileWrite)(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len);

typedef struct {
	BlockDeviceStructSize *structSize;
	BlockDeviceGetLastResult *getLastResult;

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

uint16_t blockDeviceRead(uint32_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t blockDeviceWrite(uint32_t addr, const uint8_t *data, uint16_t len, void *userData);

uint32_t blockDeviceGetSize(void *userData);

#endif
