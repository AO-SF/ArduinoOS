#include "minifs.h"

BlockDeviceReturnType blockDeviceMiniFsMount(void *fs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsUnmount(void *fs) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsVerify(const void *fs) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsDirGetChildCount(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsDirIsEmpty(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsDirCreate(void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsFileExists(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsFileGetLen(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsFileResize(void *fs, KStr path, uint32_t newSize) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsFileDelete(void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len) {
	return BlockDeviceReturnTypeIOError; // TODO: this
}

BlockDeviceReturnType blockDeviceMiniFsFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len) {
	return BlockDeviceReturnTypeIOError; // TODO: this
}
