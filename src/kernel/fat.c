#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"
#include "log.h"

#define FATPATHMAX 64 // for compatability with rest of OS

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

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t fatRead(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);
uint32_t fatWrite(const Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);

bool fatRead8(const Fat *fs, uint32_t addr, uint8_t *value);
bool fatRead16(const Fat *fs, uint32_t addr, uint16_t *value);
bool fatRead32(const Fat *fs, uint32_t addr, uint32_t *value);

bool fatReadBpbBytsPerSec(const Fat *fs, uint16_t *value);
bool fatReadBpbRootEntCnt(const Fat *fs, uint16_t *value);
bool fatReadBpbFatSz16(const Fat *fs, uint16_t *value);
bool fatReadBpbFatSz32(const Fat *fs, uint32_t *value);
bool fatReadBpbFatSz(const Fat *fs, uint32_t *value); // picks whichever of 16 and 32 bit versions is non-zero
bool fatReadBpbTotSec16(const Fat *fs, uint16_t *value);
bool fatReadBpbTotSec32(const Fat *fs, uint32_t *value);
bool fatReadBpbTotSec(const Fat *fs, uint32_t *value); // picks whichever of 16 and 32 bit versions is non-zero
bool fatReadBpbResvdSecCnt(const Fat *fs, uint16_t *value);
bool fatReadBpbNumFats(const Fat *fs, uint8_t *value);
bool fatReadBpbSecPerClus(const Fat *fs, uint8_t *value);

uint16_t fatGetBytesPerSector(const Fat *fs);
FatType fatGetFatType(const Fat *fs);
uint16_t fatGetFatSector(const Fat *fs);
uint32_t fatGetFatOffset(const Fat *fs);
uint16_t fatGetRootDirSector(const Fat *fs);
uint32_t fatGetRootDirOffset(const Fat *fs);

const char *fatTypeToString(FatType type);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool fatMountFast(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData) {
	// Set Fat struct fields
	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	fs->userData=functorUserData;

	// Read file system info
	fs->bytesPerSector=0;
	uint16_t bpbRootEntCnt=0, bpbResvdSecCnt=0;
	uint32_t bpbFatSz=0, bpbTotSec=0;
	uint8_t bpbNumFats=0, bpbSecPerClus=0;

	bool error=false;
	error|=!fatReadBpbBytsPerSec(fs, &fs->bytesPerSector);
	error|=!fatReadBpbRootEntCnt(fs, &bpbRootEntCnt);
	error|=!fatReadBpbFatSz(fs, &bpbFatSz);
	error|=!fatReadBpbTotSec(fs, &bpbTotSec);
	error|=!fatReadBpbResvdSecCnt(fs, &bpbResvdSecCnt);
	error|=!fatReadBpbNumFats(fs, &bpbNumFats);
	error|=!fatReadBpbSecPerClus(fs, &bpbSecPerClus);

	if (error) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read BPB\n"));
		return false;
	}

	// Calculate further info
	uint16_t rootDirSizeSectors=((bpbRootEntCnt*32)+(fs->bytesPerSector-1))/fs->bytesPerSector; // size of root directory in sectors (actually 0 if FAT32)
	uint32_t dataSectors=bpbTotSec-(bpbResvdSecCnt+(bpbNumFats*bpbFatSz)+rootDirSizeSectors);
	uint32_t totalClusters=dataSectors/bpbSecPerClus;

	fs->fatSector=bpbResvdSecCnt;
	fs->rootDirSector=bpbResvdSecCnt+bpbNumFats*bpbFatSz; // TODO: this is wrong if FAT32 - need to read from extended bpb thing, want to make a fat32 volume to test with first

	fs->type=FatTypeFAT32;
	if (fs->bytesPerSector==0)
		fs->type=FatTypeExFAT;
	else if(totalClusters<4085)
		fs->type=FatTypeFAT12;
	else if(totalClusters<65525)
		fs->type=FatTypeFAT16;

	return true;
}

bool fatMountSafe(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData) {
	// Use mount fast first to do basic checks and fill in Fat structure.
	if (!fatMountFast(fs, readFunctor, writeFunctor, functorUserData))
		return false;

	// Verify two magic bytes at end of first block
	uint8_t byte;
	if (fatRead(fs, 510, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read first magic byte at addr 510\n"));
		goto error;
	}
	if (byte!=0x55) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad first magic byte value 0x%02X at addr 510 (expecting 0x%02X)\n"), byte, 0x55);
		goto error;
	}
	if (fatRead(fs, 511, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read second magic byte at addr 511\n"));
		goto error;
	}
	if (byte!=0xAA) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad second magic byte value 0x%02X at addr 511 (expecting 0x%02X)\n"), byte, 0xAA);
		goto error;
	}

	// Check file system type
	switch(fatGetFatType(fs)) {
		case FatTypeFAT12:
		case FatTypeFAT16:
		case FatTypeFAT32:
		break;
		case FatTypeExFAT:
			kernelLog(LogTypeWarning, kstrP("fatMount: unsupported type: %s\n"), fatTypeToString(fatGetFatType(fs)));
			goto error;
		break;
	}

	return true;

	error:
	fatUnmount(fs);
	return false;
}

void fatUnmount(Fat *fs) {
}

void fatDebug(const Fat *fs) {
	// Begin
	kernelLog(LogTypeInfo, kstrP("fatDebug:\n"));

	// Read file system info
	uint16_t bpbBytsPerSec=0, bpbRootEntCnt=0, bpbResvdSecCnt=0;
	uint32_t bpbFatSz=0, bpbTotSec=0;
	uint8_t bpbNumFats=0, bpbSecPerClus=0;

	bool error=false;
	error|=!fatReadBpbBytsPerSec(fs, &bpbBytsPerSec);
	error|=!fatReadBpbRootEntCnt(fs, &bpbRootEntCnt);
	error|=!fatReadBpbFatSz(fs, &bpbFatSz);
	error|=!fatReadBpbTotSec(fs, &bpbTotSec);
	error|=!fatReadBpbResvdSecCnt(fs, &bpbResvdSecCnt);
	error|=!fatReadBpbNumFats(fs, &bpbNumFats);
	error|=!fatReadBpbSecPerClus(fs, &bpbSecPerClus);

	if (error)
		kernelLog(LogTypeInfo, kstrP("	(warning: error during reading, following data may be inaccurate)\n"));

	// Calculate further info
	uint16_t rootDirSizeSectors=((bpbRootEntCnt*32)+(bpbBytsPerSec-1))/bpbBytsPerSec; // size of root directory in sectors (actually 0 if FAT32)
	uint32_t dataSectors=bpbTotSec-(bpbResvdSecCnt+(bpbNumFats*bpbFatSz)+rootDirSizeSectors);
	uint32_t totalClusters=dataSectors/bpbSecPerClus;

	uint32_t fatOffset=bpbResvdSecCnt*bpbBytsPerSec;
	uint32_t rootDirOffset=(bpbResvdSecCnt+bpbNumFats*bpbFatSz)*bpbBytsPerSec; // not correct if FAT32

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

	// Inspect root directory
	kernelLog(LogTypeInfo, kstrP("	reading root dir:\n"));
	for(unsigned i=0; i<16; ++i) {
		uint32_t entryOffset=rootDirOffset+i*32;

		// Read fields for this entry
		char fileName[16]={0};
		fatRead(fs, entryOffset+0, (uint8_t *)fileName, 8);

		if (fileName[0]==0x00) {
			kernelLog(LogTypeInfo, kstrP("		%03u: unused (final)\n"), i);
			break;
		} else if (((uint8_t)fileName[0])==0xE5) {
			kernelLog(LogTypeInfo, kstrP("		%03u: unused (deleted)\n"), i);
			continue;
		} else if (fileName[0]==0x2E) {
			kernelLog(LogTypeInfo, kstrP("		%03u: 'dot entry')\n"), i);
			continue;
		}

		char fileExtension[8]={0};
		fatRead(fs, entryOffset+8, (uint8_t *)fileExtension, 3);

		uint8_t fileAttributes;
		fatReadDirEntryAttributes(fs, entryOffset, &fileAttributes);

		uint32_t fileSize;
		fatReadDirEntrySize(fs, entryOffset, &fileSize);

		uint32_t firstCluster;
		fatReadDirEntryFirstCluster(fs, entryOffset, &firstCluster);

		// Print info
		kernelLog(LogTypeInfo, kstrP("		%03u: %s.%s (%u bytes, attrs = 0x%02X - RO=%u, HIDE=%u, SYS=%u, DIR=%u, firstCluster=%u=0x%08X)\n"), i, fileName, fileExtension, fileSize, fileAttributes, (fileAttributes & FatDirEntryAttributesReadOnly)!=0, (fileAttributes & FatDirEntryAttributesHidden)!=0, (fileAttributes & FatDirEntryAttributesSystem)!=0, (fileAttributes & FatDirEntryAttributesSubDir)!=0, firstCluster, firstCluster);
	}
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

bool fatReadBpbBytsPerSec(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 11, value);
}

bool fatReadBpbRootEntCnt(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 17, value);
}

bool fatReadBpbFatSz16(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 22, value);
}

bool fatReadBpbFatSz32(const Fat *fs, uint32_t *value) {
	return fatRead32(fs, 36, value);
}

bool fatReadBpbFatSz(const Fat *fs, uint32_t *value) {
	uint16_t value16;
	if (!fatReadBpbFatSz16(fs, &value16))
		return false;
	if (value16>0) {
		*value=value16;
		return true;
	}
	if (!fatReadBpbFatSz32(fs, value))
		return false;
	return (*value>0);
}

bool fatReadBpbTotSec16(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 9, value);
}

bool fatReadBpbTotSec32(const Fat *fs, uint32_t *value) {
	return fatRead32(fs, 32, value);
}

bool fatReadBpbTotSec(const Fat *fs, uint32_t *value) {
	uint16_t value16;
	if (!fatReadBpbTotSec16(fs, &value16))
		return false;
	if (value16>0) {
		*value=value16;
		return true;
	}
	if (!fatReadBpbTotSec32(fs, value))
		return false;
	return (*value>0);
}

bool fatReadBpbResvdSecCnt(const Fat *fs, uint16_t *value) {
	return fatRead16(fs, 14, value);
}

bool fatReadBpbNumFats(const Fat *fs, uint8_t *value) {
	return fatRead8(fs, 16, value);
}

bool fatReadBpbSecPerClus(const Fat *fs, uint8_t *value) {
	return fatRead8(fs, 13, value);
}

uint16_t fatGetBytesPerSector(const Fat *fs) {
	return fs->bytesPerSector;
}

FatType fatGetFatType(const Fat *fs) {
	return fs->type;
}

uint16_t fatGetFatSector(const Fat *fs) {
	return fs->fatSector;
}

uint32_t fatGetFatOffset(const Fat *fs) {
	return fatGetFatSector(fs)*fatGetBytesPerSector(fs);
}

uint16_t fatGetRootDirSector(const Fat *fs) {
	return fs->rootDirSector;
}

uint32_t fatGetRootDirOffset(const Fat *fs) {
	return fatGetRootDirSector(fs)*fatGetBytesPerSector(fs);
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
