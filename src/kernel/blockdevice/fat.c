#include <assert.h>

#include "../log.h"
#include "fat.h"

typedef enum {
	FatTypeFAT12,
	FatTypeFAT16,
	FatTypeFAT32,
	FatTypeExFAT,
} FatType;

typedef enum {
	FatDirEntryNameFirstByteUnusedFinal=0x00, // entry is free has never bene used
	FatDirEntryNameFirstByteEscape0xE5=0x05, // file actually starts with 0xE5
	FatDirEntryNameFirstByteUnusedDeleted=0x2E, // previously used entry which has since been deleted and is now free
	FatDirEntryNameFirstByteDotEntry=0xE5, // . or ..
} FatDirEntryNameFirstByte;

typedef enum {
	FatReadDirEntryNameResultError, // e.g. read error
	FatReadDirEntryNameResultSuccess, // name read successfully
	FatReadDirEntryNameResultUnused, // unused entry
	FatReadDirEntryNameResultEnd, // end of list
} FatReadDirEntryNameResult;

typedef enum {
	FatDirEntryAttributesNone=0x00,
	FatDirEntryAttributesReadOnly=0x01,
	FatDirEntryAttributesHidden=0x02,
	FatDirEntryAttributesSystem=0x04,
	FatDirEntryAttributesVolumeLabel=0x08,
	FatDirEntryAttributesSubDir=0x10,
	FatDirEntryAttributesArchive=0x20,
	FatDirEntryAttributesDevice=0x40,
	FatDirEntryAttributesReserved=0x80,
} FatDirEntryAttributes;

typedef enum {
	FatClusterTypeError, // e.g. error reading from disk when determing type
	FatClusterTypeFree, // unused
	FatClusterTypeData, // currently used for data
	FatClusterTypeReserved, // reserved or bad cluster
	FatClusterTypeEndOfChain, // last cluster in file
} FatClusterType;

typedef struct {
	BlockDeviceReadFunctor *readFunctor;
	BlockDeviceWriteFunctor *writeFunctor;
	void *userData;

	uint16_t bytesPerSector;

	// sectors in general need up to 28 bits in FAT32 but these special sectors should always be in first sectorsize*64kb
	uint16_t fatSector;
	uint16_t rootDirSector;
	uint16_t firstDataSector;

	uint8_t type;
	uint8_t sectorsPerCluster;
} Fat;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t blockDeviceFatRead(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);
uint32_t blockDeviceFatWrite(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);

bool blockDeviceFatRead8(const Fat *fs, uint32_t addr, uint8_t *value);
bool blockDeviceFatRead16(const Fat *fs, uint32_t addr, uint16_t *value);
bool blockDeviceFatRead32(const Fat *fs, uint32_t addr, uint32_t *value);

bool blockDeviceFatReadBpbBytsPerSec(const Fat *fs, uint16_t *value);
bool blockDeviceFatReadBpbRootEntCnt(const Fat *fs, uint16_t *value);
bool blockDeviceFatReadBpbFatSz16(const Fat *fs, uint16_t *value);
bool blockDeviceFatReadBpbFatSz32(const Fat *fs, uint32_t *value);
bool blockDeviceFatReadBpbFatSz(const Fat *fs, uint32_t *value); // picks whichever of 16 and 32 bit versions is non-zero
bool blockDeviceFatReadBpbTotSec16(const Fat *fs, uint16_t *value);
bool blockDeviceFatReadBpbTotSec32(const Fat *fs, uint32_t *value);
bool blockDeviceFatReadBpbTotSec(const Fat *fs, uint32_t *value); // picks whichever of 16 and 32 bit versions is non-zero
bool blockDeviceFatReadBpbResvdSecCnt(const Fat *fs, uint16_t *value);
bool blockDeviceFatReadBpbNumFats(const Fat *fs, uint8_t *value);
bool blockDeviceFatReadBpbSecPerClus(const Fat *fs, uint8_t *value);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

	return BlockDeviceReturnTypeReadError; // TODO: this
BlockDeviceReturnType blockDeviceFatMount(void *gfs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData) {
}

BlockDeviceReturnType blockDeviceFatUnmount(void *fs) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatVerify(const void *fs) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirGetChildCount(const void *fs, KStr path, uint16_t *count) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirIsEmpty(const void *fs, KStr path, bool *isEmpty) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirCreate(void *fs, KStr path) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileExists(const void *fs, KStr path, bool *exists) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileGetLen(const void *fs, KStr path, uint32_t *size) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileResize(void *fs, KStr path, uint32_t newSize) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileDelete(void *fs, KStr path) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len, uint16_t *count) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len, uint16_t *count) {
	return BlockDeviceReturnTypeReadError; // TODO: this
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint32_t blockDeviceFatRead(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	return fs->readFunctor(addr, data, len, fs->userData);
}

uint32_t blockDeviceFatWrite(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	if (fs->writeFunctor==NULL)
		return 0;
	return fs->writeFunctor(addr, data, len, fs->userData);
}

bool blockDeviceFatRead8(const Fat *fs, uint32_t addr, uint8_t *value) {
	return (blockDeviceFatRead(fs, addr, value, 1)==1);
}

bool blockDeviceFatRead16(const Fat *fs, uint32_t addr, uint16_t *value) {
	// Note: little-endian
	uint8_t lower, upper;
	if (!blockDeviceFatRead8(fs, addr, &lower) || !blockDeviceFatRead8(fs, addr+1, &upper))
		return false;
	*value=(((uint16_t)upper)<<8)|lower;
	return true;
}

bool blockDeviceFatRead32(const Fat *fs, uint32_t addr, uint32_t *value) {
	// Note: little-endian
	uint16_t lower, upper;
	if (!blockDeviceFatRead16(fs, addr, &lower) || !blockDeviceFatRead16(fs, addr+2, &upper))
		return false;
	*value=(((uint32_t)upper)<<16)|lower;
	return true;
}

bool blockDeviceFatReadBpbBytsPerSec(const Fat *fs, uint16_t *value) {
	return blockDeviceFatRead16(fs, 11, value);
}

bool blockDeviceFatReadBpbRootEntCnt(const Fat *fs, uint16_t *value) {
	return blockDeviceFatRead16(fs, 17, value);
}

bool blockDeviceFatReadBpbFatSz16(const Fat *fs, uint16_t *value) {
	return blockDeviceFatRead16(fs, 22, value);
}

bool blockDeviceFatReadBpbFatSz32(const Fat *fs, uint32_t *value) {
	return blockDeviceFatRead32(fs, 36, value);
}

bool blockDeviceFatReadBpbFatSz(const Fat *fs, uint32_t *value) {
	uint16_t value16;
	if (!blockDeviceFatReadBpbFatSz16(fs, &value16))
		return false;
	if (value16>0) {
		*value=value16;
		return true;
	}
	if (!blockDeviceFatReadBpbFatSz32(fs, value))
		return false;
	return (*value>0);
}

bool blockDeviceFatReadBpbTotSec16(const Fat *fs, uint16_t *value) {
	return blockDeviceFatRead16(fs, 9, value);
}

bool blockDeviceFatReadBpbTotSec32(const Fat *fs, uint32_t *value) {
	return blockDeviceFatRead32(fs, 32, value);
}

bool blockDeviceFatReadBpbTotSec(const Fat *fs, uint32_t *value) {
	uint16_t value16;
	if (!blockDeviceFatReadBpbTotSec16(fs, &value16))
		return false;
	if (value16>0) {
		*value=value16;
		return true;
	}
	if (!blockDeviceFatReadBpbTotSec32(fs, value))
		return false;
	return (*value>0);
}

bool blockDeviceFatReadBpbResvdSecCnt(const Fat *fs, uint16_t *value) {
	return blockDeviceFatRead16(fs, 14, value);
}

bool blockDeviceFatReadBpbNumFats(const Fat *fs, uint8_t *value) {
	return blockDeviceFatRead8(fs, 16, value);
}

bool blockDeviceFatReadBpbSecPerClus(const Fat *fs, uint8_t *value) {
	return blockDeviceFatRead8(fs, 13, value);
}
