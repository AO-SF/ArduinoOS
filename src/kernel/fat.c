#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"
#include "log.h"

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
uint8_t fatGetSectorsPerCluster(const Fat *fs);
uint32_t fatGetFirstSectorForCluster(const Fat *fs, uint16_t cluster);
uint32_t fatGetOffsetForCluster(const Fat *fs, uint16_t cluster);
uint16_t fatGetClusterSize(const Fat *fs); // size of clusters in bytes

void fatReadDir(const Fat *fs, uint32_t offset);
bool fatReadDirEntryAttributes(const Fat *fs, uint32_t dirEntryOffset, uint8_t *attributes);
bool fatReadDirEntrySize(const Fat *fs, uint32_t dirEntryOffset, uint32_t *size);
bool fatReadDirEntryFirstCluster(const Fat *fs, uint32_t dirEntryOffset, uint32_t *cluster);
FatReadDirEntryNameResult fatReadDirEntryName(const Fat *fs, uint32_t dirEntryOffset,  char name[FATPATHMAX]);

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
	fs->sectorsPerCluster=0;
	uint16_t bpbRootEntCnt=0, bpbResvdSecCnt=0;
	uint32_t bpbFatSz=0, bpbTotSec=0;
	uint8_t bpbNumFats=0;


	bool error=false;
	error|=!fatReadBpbBytsPerSec(fs, &fs->bytesPerSector);
	error|=!fatReadBpbRootEntCnt(fs, &bpbRootEntCnt);
	error|=!fatReadBpbFatSz(fs, &bpbFatSz);
	error|=!fatReadBpbTotSec(fs, &bpbTotSec);
	error|=!fatReadBpbResvdSecCnt(fs, &bpbResvdSecCnt);
	error|=!fatReadBpbNumFats(fs, &bpbNumFats);
	error|=!fatReadBpbSecPerClus(fs, &fs->sectorsPerCluster);

	if (error) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read BPB\n"));
		return false;
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

bool fatGetChildN(const Fat *fs, unsigned childNum, char childPath[FATPATHMAX]) {
	assert(childNum<FATMAXFILES);

	// Loop over directory entries
	// TODO: allow sub-directories rather than assuming root dir
	unsigned childI=0;
	for(uint32_t offset=fatGetRootDirOffset(fs); 1; offset+=32) {
		// Read filename
		switch(fatReadDirEntryName(fs, offset, childPath)) {
			case FatReadDirEntryNameResultError:
				continue;
			break;
			case FatReadDirEntryNameResultSuccess:
				// Continue to rest of logic for this entry
			break;
			case FatReadDirEntryNameResultUnused:
				// Try next entry
				continue;
			break;
			case FatReadDirEntryNameResultEnd:
				// Done
				return false;
			break;
		}

		// Read and check attributes
		uint8_t attributes;
		if (!fatReadDirEntryAttributes(fs, offset, &attributes))
			continue;

		if ((attributes & FatDirEntryAttributesVolumeLabel))
			continue;

		// Nth child?
		if (childI==childNum)
			return true;
		++childI;
	}

	return false;
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

uint16_t fatGetFirstDataSector(const Fat *fs) {
	return fs->firstDataSector;
}

uint8_t fatGetSectorsPerCluster(const Fat *fs) {
	return fs->sectorsPerCluster;
}

uint32_t fatGetFirstSectorForCluster(const Fat *fs, uint16_t cluster) {
	return (((uint32_t)(cluster-2))*fatGetSectorsPerCluster(fs))+fatGetFirstDataSector(fs);
}

uint32_t fatGetOffsetForCluster(const Fat *fs, uint16_t cluster) {
	return fatGetFirstSectorForCluster(fs, cluster)*fatGetBytesPerSector(fs);
}

uint16_t fatGetClusterSize(const Fat *fs) {
	return fatGetSectorsPerCluster(fs)*fatGetBytesPerSector(fs);
}

void fatReadDir(const Fat *fs, uint32_t offset) {
	// Loop over directory entries
	for(unsigned i=0; 1; ++i,offset+=32) {
		// Read filename
		char fileName[FATPATHMAX]={0};
		switch(fatReadDirEntryName(fs, offset, fileName)) {
			case FatReadDirEntryNameResultError:
				continue;
			break;
			case FatReadDirEntryNameResultSuccess:
				// Continue to rest of logic for this entry
			break;
			case FatReadDirEntryNameResultUnused:
				// Try next entry
				continue;
			break;
			case FatReadDirEntryNameResultEnd:
				// Done
				return;
			break;
		}

		// Read and check attributes
		uint8_t attributes;
		if (!fatReadDirEntryAttributes(fs, offset, &attributes))
			continue;

		if ((attributes & FatDirEntryAttributesHidden) || (attributes & FatDirEntryAttributesVolumeLabel))
			continue;

		// If file not directory then read size
		uint32_t size=0;
		if (!(attributes & FatDirEntryAttributesSubDir))
			if (!fatReadDirEntrySize(fs, offset, &size))
				continue;

		// Print line for this entry
		if (!(attributes & FatDirEntryAttributesSubDir))
			kernelLog(LogTypeInfo, kstrP("%s %u bytes\n"), fileName, size);
		else
			kernelLog(LogTypeInfo, kstrP("%s\n"), fileName);
	}
}

bool fatReadDirEntryAttributes(const Fat *fs, uint32_t dirEntryOffset, uint8_t *attributes) {
	return fatRead8(fs, dirEntryOffset+11, attributes);
}

bool fatReadDirEntrySize(const Fat *fs, uint32_t dirEntryOffset, uint32_t *size) {
	return fatRead32(fs, dirEntryOffset+28, size);
}

bool fatReadDirEntryFirstCluster(const Fat *fs, uint32_t dirEntryOffset, uint32_t *cluster) {
	switch(fatGetFatType(fs)) {
		case FatTypeFAT12:
		case FatTypeFAT16: {
			uint16_t clusterLower;
			bool result=fatRead16(fs, dirEntryOffset+26, &clusterLower);
			*cluster=clusterLower;
			return result;
		} break;
		case FatTypeFAT32: {
			uint16_t clusterLower, clusterUpper;
			bool result=(fatRead16(fs, dirEntryOffset+26, &clusterLower) & fatRead16(fs, dirEntryOffset+20, &clusterUpper));
			*cluster=clusterLower|(((uint32_t)clusterUpper)<<16);
			return result;
		} break;
		case FatTypeExFAT:
			// Unsupported
			return false;
		break;
	}

	return false;
}

FatReadDirEntryNameResult fatReadDirEntryName(const Fat *fs, uint32_t dirEntryOffset,  char name[FATPATHMAX]) {
	// Read name
	if (fatRead(fs, dirEntryOffset+0, (uint8_t *)name, 11)!=11)
		return FatReadDirEntryNameResultError;

	// Check for special first byte
	if (((uint8_t)name[0])==FatDirEntryNameFirstByteUnusedFinal)
		return FatReadDirEntryNameResultEnd;
	if (((uint8_t)name[0])==FatDirEntryNameFirstByteDotEntry || ((uint8_t)name[0])==FatDirEntryNameFirstByteUnusedDeleted)
		return FatReadDirEntryNameResultUnused;
	if (((uint8_t)name[0])==FatDirEntryNameFirstByteEscape0xE5)
		name[0]=0xE5;

	// VFAT long name?
	if (name[6]=='~' && name[7]=='1') {
		// Loop backwards over preceeding dir entries to accumulate the name
		uint8_t nameIndex=0;
		while(1) {
			// Ready 'phony' dir entry preceeding the last
			dirEntryOffset-=32;
			uint8_t buffer[32];
			if (fatRead(fs, dirEntryOffset, buffer, 32)!=32)
				return FatReadDirEntryNameResultError;

			// Ensure attributes, type and first cluster fields are what we expect
			if (buffer[11]!=0x0F || buffer[12]!=0 || buffer[26]!=0 || buffer[27]!=0)
				return FatReadDirEntryNameResultError;

			// Read UCS2 characters and store into name as ASCII
			for(uint8_t i=0; i<10; i+=2) {
				if (nameIndex+1>=FATPATHMAX)
					break;
				name[nameIndex]=buffer[1+i];
				if (buffer[1+i+1]!=0)
					name[nameIndex]='?';
				else if (name[nameIndex]=='\0')
					return FatReadDirEntryNameResultSuccess;
				++nameIndex;
			}
			for(uint8_t i=0; i<12; i+=2) {
				if (nameIndex+1>=FATPATHMAX)
					break;
				name[nameIndex]=buffer[14+i];
				if (buffer[14+i+1]!=0)
					name[nameIndex]='?';
				else if (name[nameIndex]=='\0')
					return FatReadDirEntryNameResultSuccess;
				++nameIndex;
			}
			for(uint8_t i=0; i<4; i+=2) {
				if (nameIndex+1>=FATPATHMAX)
					break;
				name[nameIndex]=buffer[28+i];
				if (buffer[28+i+1]!=0)
					name[nameIndex]='?';
				else if (name[nameIndex]=='\0')
					return FatReadDirEntryNameResultSuccess;
				++nameIndex;
			}

			// Last entry which is part of the long name?
			if ((buffer[0] & 0x40))
				break;
		}

		name[nameIndex]='\0'; // might not be needed but better safe than sorry

		return FatReadDirEntryNameResultSuccess;
	}

	// Standard 8.3 filename - remove padding and add dot if needed
	uint8_t p1End;
	for (p1End=7; p1End>0; --p1End) {
		// strip padding from name part
		if (name[p1End]!=' ')
			break;
		name[p1End]='\0';
	}

	if (name[8]==' ') {
		// no extension - return now
		name[8]='\0';
		return FatReadDirEntryNameResultSuccess;
	}

	if (name[9]==' ') name[9]='\0'; // truncate extension if it has been padded
	if (name[10]==' ') name[10]='\0';

	name[11]=name[10]; // shift extension to preserve it and make space for the dot in the case where the name uses all 8 bytes
	name[10]=name[9];
	name[9]=name[8];

	name[++p1End]='.'; // concatenate two parts with a dot between
	name[++p1End]=name[9];
	name[++p1End]=name[10];
	name[++p1End]=name[11];
	name[++p1End]='\0';

	return FatReadDirEntryNameResultSuccess;
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
