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

uint16_t blockDeviceFatGetBytesPerSector(const Fat *fs);
FatType blockDeviceFatGetFatType(const Fat *fs);
uint16_t blockDeviceFatGetFatSector(const Fat *fs);
uint32_t blockDeviceFatGetFatOffset(const Fat *fs);
uint16_t blockDeviceFatGetRootDirSector(const Fat *fs);
uint32_t blockDeviceFatGetRootDirOffset(const Fat *fs);
uint8_t blockDeviceFatGetSectorsPerCluster(const Fat *fs);
uint32_t blockDeviceFatGetFirstSectorForCluster(const Fat *fs, uint16_t cluster);
uint32_t blockDeviceFatGetOffsetForCluster(const Fat *fs, uint16_t cluster);
uint16_t blockDeviceFatGetClusterSize(const Fat *fs); // size of clusters in bytes

const char *blockDeviceFatTypeToString(FatType type);
const char *blockDeviceFatClusterTypeToString(FatClusterType type);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

uint16_t blockDeviceFatStructSize(void) {
	return sizeof(Fat);
}

uint32_t blockDeviceFatGetLastResult(const void *fs) {
	// TODO: this in due course
	return 0;
}

BlockDeviceReturnType blockDeviceFatMount(void *gfs, void *userData) {
	Fat *fs=(Fat *)gfs;

	// Set Fat struct fields
	fs->userData=userData;

	// Read file system info
	fs->bytesPerSector=0;
	fs->sectorsPerCluster=0;
	uint16_t bpbRootEntCnt=0, bpbResvdSecCnt=0;
	uint32_t bpbFatSz=0, bpbTotSec=0;
	uint8_t bpbNumFats=0;

	bool error=false;
	error|=!blockDeviceFatReadBpbBytsPerSec(fs, &fs->bytesPerSector);
	error|=!blockDeviceFatReadBpbRootEntCnt(fs, &bpbRootEntCnt);
	error|=!blockDeviceFatReadBpbFatSz(fs, &bpbFatSz);
	error|=!blockDeviceFatReadBpbTotSec(fs, &bpbTotSec);
	error|=!blockDeviceFatReadBpbResvdSecCnt(fs, &bpbResvdSecCnt);
	error|=!blockDeviceFatReadBpbNumFats(fs, &bpbNumFats);
	error|=!blockDeviceFatReadBpbSecPerClus(fs, &fs->sectorsPerCluster);

	if (error) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read BPB\n"));
		return BlockDeviceReturnTypeIOError;
	}

	// Calculate further info
	uint16_t rootDirSizeSectors=((bpbRootEntCnt*32)+(fs->bytesPerSector-1))/fs->bytesPerSector; // size of root directory in sectors (actually 0 if FAT32)
	uint32_t dataSectors=bpbTotSec-(bpbResvdSecCnt+(bpbNumFats*bpbFatSz)+rootDirSizeSectors);
	uint32_t totalClusters=dataSectors/fs->sectorsPerCluster;

	fs->fatSector=bpbResvdSecCnt;
	fs->rootDirSector=bpbResvdSecCnt+bpbNumFats*bpbFatSz; // TODO: this is wrong if FAT32 - need to read from extended bpb thing, want to make a fat32 volume to test with first

	fs->type=FatTypeFAT32;
	if (fs->bytesPerSector==0)
		fs->type=FatTypeExFAT;
	else if(totalClusters<4085)
		fs->type=FatTypeFAT12;
	else if(totalClusters<65525)
		fs->type=FatTypeFAT16;

	fs->firstDataSector=fs->rootDirSector+rootDirSizeSectors; // TODO: this could be different for FAT32 (or perhaps works anyway because rootDirSizeSectors is 0?)

	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFatUnmount(void *fs) {
	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFatVerify(const void *gfs) {
	Fat *fs=(Fat *)gfs;

	// Verify two magic bytes at end of first block
	uint8_t byte;
	if (blockDeviceFatRead(fs, 510, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read first magic byte at addr 510\n"));
		return BlockDeviceReturnTypeCorruptVolume;
	}
	if (byte!=0x55) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad first magic byte value 0x%02X at addr 510 (expecting 0x%02X)\n"), byte, 0x55);
		return BlockDeviceReturnTypeCorruptVolume;
	}
	if (blockDeviceFatRead(fs, 511, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read second magic byte at addr 511\n"));
		return BlockDeviceReturnTypeCorruptVolume;
	}
	if (byte!=0xAA) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad second magic byte value 0x%02X at addr 511 (expecting 0x%02X)\n"), byte, 0xAA);
		return BlockDeviceReturnTypeCorruptVolume;
	}

	// Check file system type
	switch(blockDeviceFatGetFatType(fs)) {
		case FatTypeFAT12:
		case FatTypeFAT16:
		case FatTypeFAT32:
		break;
		case FatTypeExFAT:
			kernelLog(LogTypeWarning, kstrP("fatMount: unsupported type: %s\n"), blockDeviceFatTypeToString(blockDeviceFatGetFatType(fs)));
			return BlockDeviceReturnTypeUnsupported;
		break;
	}

	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceFatDirGetChildN(const void *fs, KStr path, uint16_t childNum, char childPath[BlockDevicePathMax]) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirGetChildCount(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirIsEmpty(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatDirCreate(void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileExists(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileGetLen(const void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileResize(void *fs, KStr path, uint32_t newSize) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileDelete(void *fs, KStr path) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileRead(const void *fs, KStr path, uint32_t offset, uint8_t *data, uint16_t len) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

BlockDeviceReturnType blockDeviceFatFileWrite(void *fs, KStr path, uint32_t offset, const uint8_t *data, uint16_t len) {
	return BlockDeviceReturnTypeUnsupported; // TODO: this
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint32_t blockDeviceFatRead(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	return blockDeviceRead(addr, data, len, fs->userData);
}

uint32_t blockDeviceFatWrite(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	return blockDeviceWrite(addr, data, len, fs->userData);
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

uint16_t blockDeviceFatGetBytesPerSector(const Fat *fs) {
	return fs->bytesPerSector;
}

FatType blockDeviceFatGetFatType(const Fat *fs) {
	return fs->type;
}

uint16_t blockDeviceFatGetFatSector(const Fat *fs) {
	return fs->fatSector;
}

uint32_t blockDeviceFatGetFatOffset(const Fat *fs) {
	return blockDeviceFatGetFatSector(fs)*blockDeviceFatGetBytesPerSector(fs);
}

uint16_t blockDeviceFatGetRootDirSector(const Fat *fs) {
	return fs->rootDirSector;
}

uint32_t blockDeviceFatGetRootDirOffset(const Fat *fs) {
	return blockDeviceFatGetRootDirSector(fs)*blockDeviceFatGetBytesPerSector(fs);
}

uint16_t blockDeviceFatGetFirstDataSector(const Fat *fs) {
	return fs->firstDataSector;
}

uint8_t blockDeviceFatGetSectorsPerCluster(const Fat *fs) {
	return fs->sectorsPerCluster;
}

uint32_t blockDeviceFatGetFirstSectorForCluster(const Fat *fs, uint16_t cluster) {
	return (((uint32_t)(cluster-2))*blockDeviceFatGetSectorsPerCluster(fs))+blockDeviceFatGetFirstDataSector(fs);
}

uint32_t blockDeviceFatGetOffsetForCluster(const Fat *fs, uint16_t cluster) {
	return blockDeviceFatGetFirstSectorForCluster(fs, cluster)*blockDeviceFatGetBytesPerSector(fs);
}

uint16_t blockDeviceFatGetClusterSize(const Fat *fs) {
	return blockDeviceFatGetSectorsPerCluster(fs)*blockDeviceFatGetBytesPerSector(fs);
}

static const char *blockDeviceFatTypeToStringArray[]={
	[FatTypeFAT12]="FAT12",
	[FatTypeFAT16]="FAT16",
	[FatTypeFAT32]="FAT32",
	[FatTypeExFAT]="exFAT",
};
const char *blockDeviceFatTypeToString(FatType type) {
	return blockDeviceFatTypeToStringArray[type];
}

static const char *blockDeviceFatClusterTypeToStringArray[]={
	[FatClusterTypeError]="Error",
	[FatClusterTypeFree]="Free",
	[FatClusterTypeData]="Data",
	[FatClusterTypeReserved]="Reserved",
	[FatClusterTypeEndOfChain]="EndOfChain",
};
const char *blockDeviceFatClusterTypeToString(FatClusterType type) {
	return blockDeviceFatClusterTypeToStringArray[type];
}
