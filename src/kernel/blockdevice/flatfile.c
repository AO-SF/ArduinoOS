#include "flatfile.h"

typedef struct {
	BlockDeviceReadFunctor *readFunctor;
	BlockDeviceWriteFunctor *writeFunctor;
	void *userData;
} FlatFile;

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

BlockDeviceReturnType blockDeviceFlatFileDirGetChildCount(const void *fs, KStr path, uint16_t *count) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileDirIsEmpty(const void *fs, KStr path, bool *isEmpty) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileDirCreate(void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported;
}

BlockDeviceReturnType blockDeviceFlatFileFileExists(const void *fs, KStr path, bool *exists) {
	// Flatfile format is simply a single file with no sub-files or directories
	*exists=(kstrStrcmp("/", path)==0);
	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFlatFileFileGetLen(const void *fs, KStr path, uint32_t *size) {
	return BlockDeviceReturnTypeReadError; // TODO: this
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

BlockDeviceReturnType blockDeviceFlatFileFileRead(const void *gfs, KStr path, uint32_t offset, uint8_t *data, uint16_t len, uint16_t *count) {
	const FlatFile *fs=(const FlatFile *)gfs;

	// Flatfile format is simply a single file with no sub-files or directories
	if (kstrStrcmp("/", path)!=0)
		return BlockDeviceReturnTypeFileDoesNotExist;

	// Simply read data from base file
	*count=fs->readFunctor(offset, data, len, fs->userData);

	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFlatFileFileWrite(void *gfs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len, uint16_t *count) {
	FlatFile *fs=(FlatFile *)gfs;

	// Flatfile format is simply a single file with no sub-files or directories
	if (kstrStrcmp("/", path)!=0)
		return BlockDeviceReturnTypeFileDoesNotExist;

	// Simply write data to base file
	*count=fs->writeFunctor(offset, data, len, fs->userData);

	return BlockDeviceReturnTypeSuccess;
}
