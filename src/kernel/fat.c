#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"
#include "log.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t fatRead(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);
uint32_t fatWrite(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);

bool fatRead8(const Fat *fs, uint32_t addr, uint8_t *value);
bool fatRead16(const Fat *fs, uint32_t addr, uint16_t *value);
bool fatRead32(const Fat *fs, uint32_t addr, uint32_t *value);

bool fatGetBpbBytsPerSec(const Fat *fs, uint16_t *value);
bool fatGetBpbRootEntCnt(const Fat *fs, uint16_t *value);
bool fatGetBpbFatSz16(const Fat *fs, uint16_t *value);
bool fatGetBpbFatSz32(const Fat *fs, uint32_t *value);
bool fatGetBpbFatSz(const Fat *fs, uint32_t *value); // picks whichever of 16 and 32 bit versions is non-zero
bool fatGetBpbTotSec16(const Fat *fs, uint16_t *value);
bool fatGetBpbTotSec32(const Fat *fs, uint32_t *value);
bool fatGetBpbTotSec(const Fat *fs, uint32_t *value); // picks whichever of 16 and 32 bit versions is non-zero
bool fatGetBpbResvdSecCnt(const Fat *fs, uint16_t *value);
bool fatGetBpbNumFats(const Fat *fs, uint8_t *value);
bool fatGetBpbSecPerClus(const Fat *fs, uint8_t *value);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool fatMountFast(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData) {
	uint8_t byte;

	// Set Fat struct fields
	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	fs->userData=functorUserData;

	// Verify two magic bytes at end of first block
	if (fatRead(fs, 510, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read first magic byte at addr 510\n"));
		return false;
	}
	if (byte!=0x55) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad first magic byte value 0x%02X at addr 510 (expecting 0x%02X)\n"), byte, 0x55);
		return false;
	}
	if (fatRead(fs, 511, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read second magic byte at addr 511\n"));
		return false;
	}
	if (byte!=0xAA) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad second magic byte value 0x%02X at addr 511 (expecting 0x%02X)\n"), byte, 0xAA);
		return false;
	}

	return true;
}

bool fatMountSafe(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData) {
	// Use mount fast first to do basic checks and fill in Fat structure.
	if (!fatMountFast(fs, readFunctor, writeFunctor, functorUserData))
		return false;

	// TODO: this for Fat file system support .....

	return true;
}

void fatUnmount(Fat *fs) {
	// TODO: this for Fat file system support .....
}

void fatDebug(const Fat *fs) {
	// Begin
	kernelLog(LogTypeInfo, kstrP("fatDebug:\n"));

	// Read file system info
	uint16_t bpbBytsPerSec=0, bpbRootEntCnt=0, bpbResvdSecCnt=0;
	uint32_t bpbFatSz=0, bpbTotSec=0;
	uint8_t bpbNumFats=0, bpbSecPerClus=0;

	bool error=false;
	error|=!fatGetBpbBytsPerSec(fs, &bpbBytsPerSec);
	error|=!fatGetBpbRootEntCnt(fs, &bpbRootEntCnt);
	error|=!fatGetBpbFatSz(fs, &bpbFatSz);
	error|=!fatGetBpbTotSec(fs, &bpbTotSec);
	error|=!fatGetBpbResvdSecCnt(fs, &bpbResvdSecCnt);
	error|=!fatGetBpbNumFats(fs, &bpbNumFats);
	error|=!fatGetBpbSecPerClus(fs, &bpbSecPerClus);

	if (error)
		kernelLog(LogTypeInfo, kstrP("	(warning: error during reading, following data may be inaccurate)\n"));

	// Calculate further info
	uint16_t rootDirSizeSectors=((bpbRootEntCnt*32)+(bpbBytsPerSec-1))/bpbBytsPerSec; // size of root directory in sectors (actually 0 if FAT32)
	uint32_t dataSectors=bpbTotSec-(bpbResvdSecCnt+(bpbNumFats*bpbFatSz)+rootDirSizeSectors);
	uint32_t totalClusters=dataSectors/bpbSecPerClus;

	uint32_t fatOffset = bpbResvdSecCnt*bpbBytsPerSec;
	uint32_t rootDirOffset = (bpbResvdSecCnt+bpbNumFats*bpbFatSz)*bpbBytsPerSec; // not correct if FAT32

	FatType fatType;
	if (bpbBytsPerSec==0)
		fatType=FatTypeExFAT;
	else if(totalClusters<4085)
		fatType=FatTypeFAT12;
	else if(totalClusters<65525)
		fatType=FatTypeFAT16;
	else
		fatType=FatTypeFAT32;

	// Print/log file system info
	kernelLog(LogTypeInfo, kstrP("	bpbBytsPerSec=%u, bpbRootEntCnt=%u, bpbFatSz=%u\n"), bpbBytsPerSec, bpbRootEntCnt, bpbFatSz);
	kernelLog(LogTypeInfo, kstrP("	bpbTotSec=%u, bpbResvdSecCnt=%u, bpbNumFats=%u, bpbSecPerClus=%u\n"), bpbTotSec, bpbResvdSecCnt, bpbNumFats, bpbSecPerClus);
	kernelLog(LogTypeInfo, kstrP("	type=%s, rootDirSizeSectors=%u, dataSectors=%u, totalClusters=%u\n"), fatTypeToString(fatType), rootDirSizeSectors, dataSectors, totalClusters);
	kernelLog(LogTypeInfo, kstrP("	fatOffset=%u (0x%08X), rootDirOffset=%u (0x%08X)\n"), fatOffset, fatOffset, rootDirOffset, rootDirOffset);
}

static const char *fatTypeToStringArray[]={
	[FatTypeFAT12]="FAT12",
	[FatTypeFAT16]="FAT16",
	[FatTypeFAT32]="FAT32",
	[FatTypeExFAT]="exFAT",
};
const char *fatTypeToString(FatType type) {
	return fatTypeToStringArray[type];
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint32_t fatRead(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	return fs->readFunctor(addr, data, len, fs->userData);
}

uint32_t fatWrite(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	if (fs->writeFunctor==NULL)
		return 0;
	return fs->writeFunctor(addr, data, len, fs->userData);
}

bool fatRead8(const Fat *fs, uint32_t addr, uint8_t *value) {
	return (fatRead(fs, addr, value, 1)==1);
}

bool fatRead16(const Fat *fs, uint32_t addr, uint16_t *value) {
	// Note: little-endian
	uint8_t lower, upper;
	if (!fatRead8(fs, addr, &lower) || !fatRead8(fs, addr+1, &upper))
		return false;
	*value=(((uint16_t)upper)<<8)|lower;
	return true;
}

bool fatRead32(const Fat *fs, uint32_t addr, uint32_t *value) {
	// Note: little-endian
	uint16_t lower, upper;
	if (!fatRead16(fs, addr, &lower) || !fatRead16(fs, addr+2, &upper))
		return false;
	*value=(((uint32_t)upper)<<16)|lower;
	return true;
}

bool fatGetBpbBytsPerSec(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 11, value);
}

bool fatGetBpbRootEntCnt(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 17, value);
}

bool fatGetBpbFatSz16(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 22, value);
}

bool fatGetBpbFatSz32(const Fat *fs, uint32_t *value) {
	return fatRead32(fs, 36, value);
}

bool fatGetBpbFatSz(const Fat *fs, uint32_t *value) {
	uint16_t value16;
	if (!fatGetBpbFatSz16(fs, &value16))
		return false;
	if (value16>0) {
		*value=value16;
		return true;
	}
	if (!fatGetBpbFatSz32(fs, value))
		return false;
	return (*value>0);
}

bool fatGetBpbTotSec16(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 9, value);
}

bool fatGetBpbTotSec32(const Fat *fs, uint32_t *value) {
	return fatRead32(fs, 32, value);
}

bool fatGetBpbTotSec(const Fat *fs, uint32_t *value) {
	uint16_t value16;
	if (!fatGetBpbTotSec16(fs, &value16))
		return false;
	if (value16>0) {
		*value=value16;
		return true;
	}
	if (!fatGetBpbTotSec32(fs, value))
		return false;
	return (*value>0);
}

bool fatGetBpbResvdSecCnt(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 14, value);
}

bool fatGetBpbNumFats(const Fat *fs, uint8_t *value) {
	return fatRead8(fs, 16, value);
}

bool fatGetBpbSecPerClus(const Fat *fs, uint8_t *value) {
	return fatRead8(fs, 13, value);
}
