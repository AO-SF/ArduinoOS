#include "flatfile.h"

typedef struct {
	BlockDeviceReadFunctor *readFunctor;
	BlockDeviceWriteFunctor *writeFunctor;
	void *userData;
} FlatFile;

uint16_t blockDeviceFlatFileStructSize(void) {
	return sizeof(FlatFile);
}

BlockDeviceReturnType blockDeviceFlatFileMount(void *gfs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData) {
	FlatFile *fs=(FlatFile *)gfs;

	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	fs->userData=userData;

	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFlatFileUnmount(void *fs) {
	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFlatFileVerify(const void *fs) {
	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFlatFileDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileDirGetChildCount(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileDirIsEmpty(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileDirCreate(void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileFileExists(const void *fs, KStr path) {
	// Flatfile format is simply a single file with no sub-files or directories
	return (kstrStrcmp("/", path)==0);
}

BlockDeviceReturnType blockDeviceFlatFileFileGetLen(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileFileResize(void *fs, KStr path, uint32_t newSize) {
	// Cannot resize in this format - size of the single file exactly matches its backing file
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileFileDelete(void *fs, KStr path) {
	// Flatfile format is simply a single file with no sub-files or directories
	if (kstrStrcmp("/", path)!=0)
		return BlockDeviceReturnTypeFileDoesNotExist;

	// Cannot delete in this format
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileFileRead(const void *gfs, KStr path, uint32_t offset, uint8_t *data, uint16_t len) {
	const FlatFile *fs=(const FlatFile *)gfs;

	// Flatfile format is simply a single file with no sub-files or directories
	if (kstrStrcmp("/", path)!=0)
		return BlockDeviceReturnTypeFileDoesNotExist;

	// Simply read data from base file
	return fs->readFunctor(offset, data, len, fs->userData);
}

BlockDeviceReturnType blockDeviceFlatFileFileWrite(void *gfs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len) {
	FlatFile *fs=(FlatFile *)gfs;

	// Flatfile format is simply a single file with no sub-files or directories
	if (kstrStrcmp("/", path)!=0)
		return BlockDeviceReturnTypeFileDoesNotExist;

	// Simply write data to base file
	return fs->writeFunctor(offset, data, len, fs->userData);
}
