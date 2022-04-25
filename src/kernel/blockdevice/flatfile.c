#include "flatfile.h"

BlockDeviceReturnType blockDeviceFlatFileMount(void *fs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileUnmount(void *fs) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileVerify(const void *fs) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileDirGetChildCount(const void *fs, KStr path, uint16_t *count) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileDirIsEmpty(const void *fs, KStr path, bool *isEmpty) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileDirCreate(void *fs, KStr path) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileFileExists(const void *fs, KStr path, bool *exists) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileFileGetLen(const void *fs, KStr path, uint32_t *size) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileFileResize(void *fs, KStr path, uint32_t newSize) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileFileDelete(void *fs, KStr path) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len, uint16_t *count) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFlatFileFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len, uint16_t *count) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}
