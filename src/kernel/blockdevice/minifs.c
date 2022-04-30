#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"

#define MINIFSFACTOR 128u // 1<=factor<=256, increasing allows for a greater total volume size, but wastes more space padding small files (so their length is a multiple of the factor)
#define MINIFSMINSIZE 128u // lcm(factor, 2headersize)=lcm(128,2*64)=128
#define MINIFSMAXSIZE (MINIFSFACTOR*256) // we use an 8 bit value with a factor to represent the total size (with factor=128 this allows up to 32kb)

#define MINIFSMAXFILES (MINIFSFACTOR-2)

#define MiniFsPathMax (MINIFSFACTOR-1)

#define MINIFSHEADERMAGICBYTEADDR 0
#define MINIFSHEADERMAGICBYTEVALUE 53
#define MINIFSHEADERTOTALSIZEADDR (MINIFSHEADERMAGICBYTEADDR+1)
#define MINIFSHEADERFILEBASEADDR (MINIFSHEADERTOTALSIZEADDR+1)
#define MINIFSHEADERSIZE (1+1+MINIFSMAXFILES) // 64 bytes

#define MINIFSFILEMINOFFSETFACTOR MINIFSHEADERSIZE/MINIFSFACTOR // no file can be stored where the header is. note this is usually always 1
#define MINIFSFILEOFFSETINVALID 0 // this would point into the header anyway

typedef struct {
	BlockDeviceReadFunctor *readFunctor;
	BlockDeviceWriteFunctor *writeFunctor; // NULL if read-only
	void *functorUserData;
} MiniFs;

typedef struct {
	uint16_t offset;
	uint16_t filenameLen, contentLen;
	uint16_t size, spare;
} MiniFsFileInfo;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint16_t blockDeviceMiniFsGetTotalSize(const MiniFs *fs); // Total size available for whole file system (including metadata)

uint8_t blockDeviceMiniFsGetTotalSizeFactorMinusOne(const MiniFs *fs);

uint16_t blockDeviceMiniFsRead(const MiniFs *fs, uint16_t addr, uint8_t *data, uint16_t len);
uint8_t blockDeviceMiniFsReadByte(const MiniFs *fs, uint16_t addr);
uint16_t blockDeviceMiniFsWrite(MiniFs *fs, uint16_t addr, const uint8_t *data, uint16_t len);
void blockDeviceMiniFsWriteByte(MiniFs *fs, uint16_t addr, uint8_t value);

bool blockDeviceMiniFsGetFilenameFromIndex(const MiniFs *fs, uint8_t index, char filename[MiniFsPathMax]);
uint8_t blockDeviceMiniFsFilenameToIndex(const MiniFs *fs, const char *filename, uint16_t *baseOffsetPtr); // Returns MINIFSMAXFILES if no such file exists. If baseOffset is non-null then filled (to 0 on failure)
uint8_t blockDeviceMiniFsFilenameToIndexKStr(const MiniFs *fs, KStr filename, uint16_t *baseOffsetPtr);
bool blockDeviceMiniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index);
bool blockDeviceMiniFsReadFileInfoFromBaseOffset(const MiniFs *fs, MiniFsFileInfo *info, uint16_t baseOffset);
bool blockDeviceMiniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index);
uint8_t blockDeviceMiniFsGetEmptyIndex(const MiniFs *fs); // return MINIFSMAXFILES on failure

uint8_t blockDeviceMiniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor); // Returns 0 on failure to find

// The follow all return 0 if slot is unused
uint8_t blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetBaseOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetLengthOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetSizeFactorOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint8_t blockDeviceMiniFsFileGetSizeFactorFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetSizeFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetFilenameOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetFilenameLenFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetContentOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsFileGetContentLenFromIndex(const MiniFs *fs, uint8_t index);
uint16_t blockDeviceMiniFsGetFileTotalLengthFromIndex(const MiniFs *fs, uint8_t index);

uint8_t blockDeviceMiniFsFileGetBaseOffsetFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetLengthOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetSizeFactorOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint8_t blockDeviceMiniFsFileGetSizeFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetSizeFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetFilenameOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetFilenameLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetContentOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsFileGetContentLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t blockDeviceMiniFsGetFileTotalLengthFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);

void blockDeviceMiniFsSetFileTotalLengthForIndex(MiniFs *fs, uint8_t index, uint16_t newTotalLen);
void blockDeviceMiniFsSetFileSizeFactorForIndex(MiniFs *fs, uint8_t index, uint8_t newSizeFactor);

void blockDeviceMiniFsClearFileForIndex(MiniFs *fs, uint8_t index);

uint8_t blockDeviceMiniFsGetSizeFactorForTotalLength(uint16_t totalLen);

void blockDeviceMiniFsResortFileOffsets(MiniFs *fs);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

uint16_t blockDeviceMiniFsStructSize(void) {
	return sizeof(MiniFs);
}

BlockDeviceReturnType blockDeviceMiniFsMount(void *gfs, BlockDeviceReadFunctor *readFunctor, BlockDeviceWriteFunctor *writeFunctor, void *userData) {
	MiniFs *fs=(MiniFs *)gfs;

	// Simply copy IO functors
	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	fs->functorUserData=userData;

	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceMiniFsUnmount(void *fs) {
	return BlockDeviceReturnTypeSuccess;
}

BlockDeviceReturnType blockDeviceMiniFsVerify(const void *fs) {
	// Verify header
	uint8_t magicByte=blockDeviceMiniFsReadByte(fs, MINIFSHEADERMAGICBYTEADDR);
	if (magicByte!=MINIFSHEADERMAGICBYTEVALUE)
		return BlockDeviceReturnTypeCorruptVolume;

	uint16_t totalSize=blockDeviceMiniFsGetTotalSize(fs);
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return BlockDeviceReturnTypeCorruptVolume;

	// Verify files
	uint8_t prevOffsetFactor=0;
	uint8_t i=0;
	while(i<MINIFSMAXFILES) {
		// Grab offset factor from header
		uint8_t fileOffsetFactor=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, i);
		if (fileOffsetFactor==0)
			break; // end of file list

		// Check the file offset does not overlap the header
		if (fileOffsetFactor<MINIFSFILEMINOFFSETFACTOR)
			return BlockDeviceReturnTypeCorruptVolume;

		// Check the file is located after the one in the previous slot.
		if (fileOffsetFactor<prevOffsetFactor)
			return BlockDeviceReturnTypeCorruptVolume;

		// Update prev variable for next iteration
		prevOffsetFactor=fileOffsetFactor;
		++i;
	}

	// Verify subsequent header entries are unused
	while(i<MINIFSMAXFILES) {
		// Grab offset factor from header and check if used
		uint8_t fileOffsetFactor=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, i);
		if (fileOffsetFactor!=0)
			return BlockDeviceReturnTypeCorruptVolume;

		++i;
	}

	return BlockDeviceReturnTypeSuccess;
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

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint16_t blockDeviceMiniFsGetTotalSize(const MiniFs *fs) {
	return (((uint16_t)blockDeviceMiniFsGetTotalSizeFactorMinusOne(fs))+1)*MINIFSFACTOR;
}

uint8_t blockDeviceMiniFsGetTotalSizeFactorMinusOne(const MiniFs *fs) {
	return blockDeviceMiniFsReadByte(fs, MINIFSHEADERTOTALSIZEADDR);
}

uint16_t blockDeviceMiniFsRead(const MiniFs *fs, uint16_t addr, uint8_t *data, uint16_t len) {
	return fs->readFunctor(addr, data, len, fs->functorUserData);
}

uint8_t blockDeviceMiniFsReadByte(const MiniFs *fs, uint16_t addr) {
	uint8_t value;
	blockDeviceMiniFsRead(fs, addr, &value, 1);
	return value;
}

uint16_t blockDeviceMiniFsWrite(MiniFs *fs, uint16_t addr, const uint8_t *data, uint16_t len) {
	return fs->writeFunctor(addr, data, len, fs->functorUserData);
}

void blockDeviceMiniFsWriteByte(MiniFs *fs, uint16_t addr, uint8_t value) {
	blockDeviceMiniFsWrite(fs, addr, &value, 1);
}

bool blockDeviceMiniFsGetFilenameFromIndex(const MiniFs *fs, uint8_t index, char filename[MiniFsPathMax]) {
	// Is there not even a file using this slot?
	uint16_t filenameOffset=blockDeviceMiniFsFileGetFilenameOffsetFromIndex(fs, index);
	if (filenameOffset==0)
		return false;

	// Copy filename
	char *dest;
	for(dest=filename; dest+1<filename+MiniFsPathMax; dest++, filenameOffset++) {
		uint8_t src=blockDeviceMiniFsReadByte(fs, filenameOffset);
		*dest=src;
		if (src=='\0')
			break;
	}

	*dest='\0';

	return true;
}

uint8_t blockDeviceMiniFsFilenameToIndex(const MiniFs *fs, const char *filename, uint16_t *baseOffsetPtr) {
	return blockDeviceMiniFsFilenameToIndexKStr(fs, kstrS((char *)filename), baseOffsetPtr);
}

uint8_t blockDeviceMiniFsFilenameToIndexKStr(const MiniFs *fs, KStr filename, uint16_t *baseOffsetPtr) {
	// Loop over all slots looking for the given filename
	for(uint8_t index=0; index<MINIFSMAXFILES; ++index) {
		// Is there even a file using this slot?
		uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
		if (baseOffset==0)
			break; // end of list

		// Check if filename matches
		uint16_t filenameOffset=blockDeviceMiniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
		bool match=true;
		for(uint16_t i=0; 1; ++i) {
			char testChar=blockDeviceMiniFsReadByte(fs, filenameOffset++);
			char trueChar=kstrGetChar(filename, i);
			if (testChar!=trueChar) {
				match=false;
				break;
			}
			if (testChar=='\0')
				break;
		}

		if (match) {
			if (baseOffsetPtr!=NULL)
				*baseOffsetPtr=baseOffset;
			return index;
		}
	}

	if (baseOffsetPtr!=NULL)
		*baseOffsetPtr=0;
	return MINIFSMAXFILES;
}

bool blockDeviceMiniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index) {
	// Is there even a file in this slot?
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsReadFileInfoFromBaseOffset(fs, info, baseOffset);
}

bool blockDeviceMiniFsReadFileInfoFromBaseOffset(const MiniFs *fs, MiniFsFileInfo *info, uint16_t baseOffset) {
	// Is there even a file in this slot?
	info->offset=baseOffset;
	if (info->offset==0)
		return false;

	// Compute total size allocated
	info->size=blockDeviceMiniFsFileGetSizeFromBaseOffset(fs, baseOffset);

	// Compute filename length
	info->filenameLen=blockDeviceMiniFsFileGetFilenameLenFromBaseOffset(fs, baseOffset);

	// Compute content length
	info->contentLen=blockDeviceMiniFsFileGetContentLenFromBaseOffset(fs, baseOffset);

	// Compute spare space
	info->spare=info->size-blockDeviceMiniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);

	return true;
}

bool blockDeviceMiniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index) {
	return (blockDeviceMiniFsReadByte(fs, MINIFSHEADERFILEBASEADDR+index)==MINIFSFILEOFFSETINVALID);
}

uint8_t blockDeviceMiniFsGetEmptyIndex(const MiniFs *fs) {
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i)
		if (blockDeviceMiniFsIsFileSlotEmpty(fs, i))
			return i;
	return MINIFSMAXFILES;
}

uint8_t blockDeviceMiniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor) {
	// No files?
	uint8_t firstFileIndex=0; // due to sorting
	if (blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex)==0) {
		// Check for insufficent space in volume
		if (sizeFactor>blockDeviceMiniFsGetTotalSizeFactorMinusOne(fs)-MINIFSFILEMINOFFSETFACTOR+1)
			return 0;
		return MINIFSFILEMINOFFSETFACTOR;
	}

	// Check for space before first file.
	if (sizeFactor<=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex)-MINIFSFILEMINOFFSETFACTOR)
		return MINIFSFILEMINOFFSETFACTOR;

	// Check for space between files.
	uint8_t secondFileIndex;
	for(secondFileIndex=firstFileIndex+1; secondFileIndex<MINIFSMAXFILES; ++secondFileIndex) {
		// Grab current file's offset and length
		uint8_t secondFileOffsetFactor=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, secondFileIndex);
		if (secondFileOffsetFactor==0)
			break; // end of file list

		// Calculate space between this file and the last
		uint8_t firstFileOffsetFactor=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex);
		uint8_t firstFileSizeFactor=blockDeviceMiniFsFileGetSizeFactorFromIndex(fs, firstFileIndex);
		if (sizeFactor<=secondFileOffsetFactor-(firstFileOffsetFactor+firstFileSizeFactor))
			return (firstFileOffsetFactor+firstFileSizeFactor);

		// Prepare for next iteration
		firstFileIndex=secondFileIndex;
	}

	// Check for space after last file.
	uint8_t firstFileOffsetFactor=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex);
	uint8_t firstFileSizeFactor=blockDeviceMiniFsFileGetSizeFactorFromIndex(fs, firstFileIndex);
	if (sizeFactor<=((uint16_t)blockDeviceMiniFsGetTotalSizeFactorMinusOne(fs))+1-(firstFileOffsetFactor+firstFileSizeFactor))
		return (firstFileOffsetFactor+firstFileSizeFactor);

	// No free region large enough
	return 0;
}

uint8_t blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(const MiniFs *fs, uint8_t index) {
	return blockDeviceMiniFsReadByte(fs, MINIFSHEADERFILEBASEADDR+index);
}

uint16_t blockDeviceMiniFsFileGetBaseOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	return ((uint16_t)blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, index))*MINIFSFACTOR;
}

uint16_t blockDeviceMiniFsFileGetLengthOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetLengthOffsetFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsFileGetSizeFactorOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetSizeFactorOffsetFromBaseOffset(fs, baseOffset);
}

uint8_t blockDeviceMiniFsFileGetSizeFactorFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetSizeFactorFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsFileGetSizeFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetSizeFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsFileGetFilenameOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsFileGetFilenameLenFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetFilenameLenFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsFileGetContentOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsFileGetContentLenFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsFileGetContentLenFromBaseOffset(fs, baseOffset);
}

uint16_t blockDeviceMiniFsGetFileTotalLengthFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=blockDeviceMiniFsFileGetBaseOffsetFromIndex(fs, index);
	return blockDeviceMiniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);
}

uint8_t blockDeviceMiniFsFileGetBaseOffsetFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	return baseOffset/MINIFSFACTOR;
}

uint16_t blockDeviceMiniFsFileGetLengthOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	return baseOffset;
}

uint16_t blockDeviceMiniFsFileGetSizeFactorOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	if (baseOffset==0)
		return 0;
	return baseOffset+2;
}

uint8_t blockDeviceMiniFsFileGetSizeFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t sizeFactorOffset=blockDeviceMiniFsFileGetSizeFactorOffsetFromBaseOffset(fs, baseOffset);
	if (sizeFactorOffset==0)
		return 0;

	return blockDeviceMiniFsReadByte(fs, sizeFactorOffset);
}

uint16_t blockDeviceMiniFsFileGetSizeFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t sizeFactor=blockDeviceMiniFsFileGetSizeFactorFromBaseOffset(fs, baseOffset);
	if (sizeFactor==0)
		return 0;
	return sizeFactor*MINIFSFACTOR;
}

uint16_t blockDeviceMiniFsFileGetFilenameOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	if (baseOffset==0)
		return 0;
	return baseOffset+3;
}

uint16_t blockDeviceMiniFsFileGetFilenameLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t filenameOffset=blockDeviceMiniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
	uint16_t filenameLen;
	for(filenameLen=0; ; ++filenameLen) {
		uint8_t c=blockDeviceMiniFsReadByte(fs, filenameOffset+filenameLen);
		if (c=='\0')
			break;
	}
	return filenameLen;
}

uint16_t blockDeviceMiniFsFileGetContentOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	// Get filename offset and offset by filename length
	uint16_t fileOffset=blockDeviceMiniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
	if (fileOffset==0)
		return 0;
	return fileOffset+blockDeviceMiniFsFileGetFilenameLenFromBaseOffset(fs, baseOffset)+1;
}

uint16_t blockDeviceMiniFsFileGetContentLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t total=blockDeviceMiniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);
	uint16_t offset=blockDeviceMiniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
	return baseOffset+total-offset;
}

uint16_t blockDeviceMiniFsGetFileTotalLengthFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t lengthFileOffset=blockDeviceMiniFsFileGetLengthOffsetFromBaseOffset(fs, baseOffset);
	if (lengthFileOffset==0)
		return 0;

	uint16_t fileTotalLength=0;
	fileTotalLength|=blockDeviceMiniFsReadByte(fs, lengthFileOffset+0);
	fileTotalLength<<=8;
	fileTotalLength|=blockDeviceMiniFsReadByte(fs, lengthFileOffset+1);
	return fileTotalLength;
}

void blockDeviceMiniFsSetFileTotalLengthForIndex(MiniFs *fs, uint8_t index, uint16_t newTotalLen) {
	uint16_t offset=blockDeviceMiniFsFileGetLengthOffsetFromIndex(fs, index);
	assert(offset!=0);
	blockDeviceMiniFsWriteByte(fs, offset+0, (newTotalLen>>8));
	blockDeviceMiniFsWriteByte(fs, offset+1, (newTotalLen&0xFF));
}

void blockDeviceMiniFsSetFileSizeFactorForIndex(MiniFs *fs, uint8_t index, uint8_t newSizeFactor) {
	uint16_t offset=blockDeviceMiniFsFileGetSizeFactorOffsetFromIndex(fs, index);
	assert(offset!=0);
	blockDeviceMiniFsWriteByte(fs, offset, newSizeFactor);
}

void blockDeviceMiniFsClearFileForIndex(MiniFs *fs, uint8_t index) {
	// Clear all file offsets
	blockDeviceMiniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+index, MINIFSFILEOFFSETINVALID);
}

uint8_t blockDeviceMiniFsGetSizeFactorForTotalLength(uint16_t totalLen) {
	return (totalLen+MINIFSFACTOR-1)/MINIFSFACTOR;
}

void blockDeviceMiniFsResortFileOffsets(MiniFs *fs) {
	// Bubble sort - should be fast due to small entry count (MINIFSMAXFILES max but typically much smaller) and being mostly ordered due to last sort

	uint8_t max=MINIFSMAXFILES;

	bool change;
	do {
		change=false;

		for(uint8_t i=1; i<max; ++i) {
			uint8_t factorA=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, i-1);
			uint8_t factorB=blockDeviceMiniFsFileGetBaseOffsetFactorFromIndex(fs, i);
			if (factorB!=0 && (factorA==0 || factorA>factorB)) { // pushes zeros to the end, but otherwise sorts ascending
				// Swap
				change=true;
				blockDeviceMiniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+i-1, factorB);
				blockDeviceMiniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+i, factorA);
			}
		}
		--max;
	} while(change);
}
