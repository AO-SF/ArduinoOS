#include "api.h"

// Actually defined in ../kernelfs.c
uint16_t kernelFsBlockDeviceReadWrapper(uint32_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t kernelFsBlockDeviceWriteWrapper(uint32_t addr, const uint8_t *data, uint16_t len, void *userData);

uint16_t blockDeviceRead(uint32_t addr, uint8_t *data, uint16_t len, void *userData) {
	return kernelFsBlockDeviceReadWrapper(addr, data, len, userData);
}

uint16_t blockDeviceWrite(uint32_t addr, const uint8_t *data, uint16_t len, void *userData) {
	return kernelFsBlockDeviceWriteWrapper(addr, data, len, userData);
}
