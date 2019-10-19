#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"

#define MINIFSHEADERMAGICBYTEADDR 0
#define MINIFSHEADERMAGICBYTEVALUE 53
#define MINIFSHEADERTOTALSIZEADDR (MINIFSHEADERMAGICBYTEADDR+1)
#define MINIFSHEADERFILEBASEADDR (MINIFSHEADERTOTALSIZEADDR+1)
#define MINIFSHEADERSIZE (1+1+MINIFSMAXFILES) // 64 bytes

#define MINIFSFILEMINOFFSETFACTOR (MINIFSHEADERSIZE/MINIFSFACTOR) // no file can be stored where the header is
#define MINIFSFILEOFFSETINVALID 0 // this would point into the header anyway

typedef struct {
	uint16_t offset;
	uint16_t filenameLen, contentLen;
	uint16_t size, spare;
} MiniFsFileInfo;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

bool miniFsIsConsistent(const MiniFs *fs);

uint8_t miniFsGetTotalSizeFactorMinusOne(const MiniFs *fs);

uint16_t miniFsRead(const MiniFs *fs, uint16_t addr, uint8_t *data, uint16_t len);
uint8_t miniFsReadByte(const MiniFs *fs, uint16_t addr);
uint16_t miniFsWrite(MiniFs *fs, uint16_t addr, const uint8_t *data, uint16_t len);
void miniFsWriteByte(MiniFs *fs, uint16_t addr, uint8_t value);

bool miniFsGetFilenameFromIndex(const MiniFs *fs, uint8_t index, char filename[MiniFsPathMax]);
uint8_t miniFsFilenameToIndex(const MiniFs *fs, const char *filename, uint16_t *baseOffsetPtr); // Returns MINIFSMAXFILES if no such file exists. If baseOffset is non-null then filled (to 0 on failure)
bool miniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index);
bool miniFsReadFileInfoFromBaseOffset(const MiniFs *fs, MiniFsFileInfo *info, uint16_t baseOffset);
bool miniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index);
uint8_t miniFsGetEmptyIndex(const MiniFs *fs); // return MINIFSMAXFILES on failure

uint8_t miniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor); // Returns 0 on failure to find

// The follow all return 0 if slot is unused
uint8_t miniFsFileGetBaseOffsetFactorFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetBaseOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetLengthOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetSizeFactorOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint8_t miniFsFileGetSizeFactorFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetSizeFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetFilenameOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetFilenameLenFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetContentOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsFileGetContentLenFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsGetFileTotalLengthFromIndex(const MiniFs *fs, uint8_t index);

uint8_t miniFsFileGetBaseOffsetFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetLengthOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetSizeFactorOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint8_t miniFsFileGetSizeFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetSizeFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetFilenameOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetFilenameLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetContentOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsFileGetContentLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);
uint16_t miniFsGetFileTotalLengthFromBaseOffset(const MiniFs *fs, uint16_t baseOffset);

void miniFsSetFileTotalLengthForIndex(MiniFs *fs, uint8_t index, uint16_t newTotalLen);
void miniFsSetFileSizeFactorForIndex(MiniFs *fs, uint8_t index, uint8_t newSizeFactor);

void miniFsClearFileForIndex(MiniFs *fs, uint8_t index);

uint8_t miniFsGetSizeFactorForTotalLength(uint16_t totalLen);

void miniFsResortFileOffsets(MiniFs *fs);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFormat(MiniFsWriteFunctor *writeFunctor, void *functorUserData, uint16_t maxTotalSize) {
	assert(writeFunctor!=NULL);

	uint8_t temp;

	// Compute actual total size, which is rounded down to the next multiple of MINIFSFACTOR
	uint16_t totalSizeFactor=maxTotalSize/MINIFSFACTOR;
	uint16_t totalSize=totalSizeFactor*MINIFSFACTOR;

	// Sanity checks
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return false;

	// Write magic number
	temp=MINIFSHEADERMAGICBYTEVALUE;
	writeFunctor(MINIFSHEADERMAGICBYTEADDR, &temp, 1, functorUserData);

	// Write total size
	temp=totalSizeFactor-1;
	writeFunctor(MINIFSHEADERTOTALSIZEADDR, &temp, 1, functorUserData);

	// Clear file list
	temp=MINIFSFILEOFFSETINVALID;
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i)
		writeFunctor(MINIFSHEADERFILEBASEADDR+i, &temp, 1, functorUserData);

	return true;
}

bool miniFsMountFast(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor, void *functorUserData) {
	// Simply copy IO functors and clear open bitset
	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	fs->functorUserData=functorUserData;

	return true;
}

bool miniFsMountSafe(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor, void *functorUserData) {
	// Call fast function to do basic initialisation
	if (!miniFsMountFast(fs, readFunctor, writeFunctor, functorUserData))
		return false;

	// Check consistency
	if (!miniFsIsConsistent(fs))
		return false;

	return true;
}

void miniFsUnmount(MiniFs *fs) {
	// Null-op
}

bool miniFsGetReadOnly(const MiniFs *fs) {
	return (fs->writeFunctor==NULL);
}

uint16_t miniFsGetTotalSize(const MiniFs *fs) {
	return (((uint16_t)miniFsGetTotalSizeFactorMinusOne(fs))+1)*MINIFSFACTOR;
}

bool miniFsGetChildN(const MiniFs *fs, unsigned childNum, char childPath[MiniFsPathMax]) {
	assert(childNum<MINIFSMAXFILES);

	return miniFsGetFilenameFromIndex(fs, childNum, childPath);
}

uint8_t miniFsGetChildCount(const MiniFs *fs) {
	uint8_t count=0;
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i) {
		if (miniFsIsFileSlotEmpty(fs, i))
			break; // end of file list
		++count;
	}
	return count;
}

bool miniFsIsEmpty(const MiniFs *fs) {
	// As the file header list is sorted, we can simply check if the first entry is empty or not.
	return miniFsIsFileSlotEmpty(fs, 0);
}

void miniFsDebug(const MiniFs *fs) {
#ifndef ARDUINO
	printf("Volume debug:\n");
	printf("	max total size: %u bytes\n", miniFsGetTotalSize(fs));
	printf("	header size: %u bytes (%u%% of total, leaving %u bytes for file data)\n", MINIFSHEADERSIZE, (100*MINIFSHEADERSIZE)/miniFsGetTotalSize(fs), miniFsGetTotalSize(fs)-MINIFSHEADERSIZE);
	printf("	mount mode: %s\n", (miniFsGetReadOnly(fs) ? "RO" : "RW"));
	printf("	files:\n");
	printf("		ID OFFSET SIZE LENGTH SPARE FILENAME\n");

	for(uint8_t i=0; i<MINIFSMAXFILES; ++i) {
		// Parse file info
		MiniFsFileInfo fileInfo;
		if (!miniFsReadFileInfoFromIndex(fs, &fileInfo, i))
			continue;

		// Output info
		printf("		%2i %6i %4i %6i %5i ", i, fileInfo.offset, fileInfo.size, fileInfo.contentLen, fileInfo.spare);
		for(uint16_t filenameOffset=miniFsFileGetFilenameOffsetFromIndex(fs, i); ; ++filenameOffset) {
			uint8_t c=miniFsReadByte(fs, filenameOffset);
			if (c=='\0')
				break;
			printf("%c", c);
		}
		printf("\n");
	}
#endif
}

bool miniFsFileExists(const MiniFs *fs, const char *filename) {
	return (miniFsFilenameToIndex(fs, filename, NULL)!=MINIFSMAXFILES);
}

uint16_t miniFsFileGetLen(const MiniFs *fs, const char *filename) {
	// This is the length used by content, not the total len
	uint16_t baseOffset;
	miniFsFilenameToIndex(fs, filename, &baseOffset);
	return miniFsFileGetContentLenFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetSize(const MiniFs *fs, const char *filename) {
	// This is the size available for content, not the true size (so does not include the filename)
	uint16_t baseOffset;
	miniFsFilenameToIndex(fs, filename, &baseOffset);

	MiniFsFileInfo fileInfo;
	if (!miniFsReadFileInfoFromBaseOffset(fs, &fileInfo, baseOffset))
		return 0;

	return fileInfo.contentLen+fileInfo.spare;
}

bool miniFsFileCreate(MiniFs *fs, const char *filename, uint16_t contentSize) {
	// Is filename too long?
	uint16_t filenameLen=strlen(filename);
	if (filenameLen>=MiniFsPathMax)
		return false;

	// We cannot create a file if the FS is read-only
	if (miniFsGetReadOnly(fs))
		return false;

	// File already exists?
	if (miniFsFileExists(fs, filename))
		return false;

	// Find a free index slot to use
	uint8_t freeIndex=miniFsGetEmptyIndex(fs);
	if (freeIndex==MINIFSMAXFILES)
		return false;

	// Look for large enough region of free space to store the file
	uint16_t fileTotalLength=3+filenameLen+1+contentSize;
	uint8_t fileSizeFactor=miniFsGetSizeFactorForTotalLength(fileTotalLength);

	uint8_t fileOffsetFactor=miniFsFindFreeRegionFactor(fs, fileSizeFactor);
	if (fileOffsetFactor==0)
		return false;

	// Insert file offset factor into header
	miniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+freeIndex, fileOffsetFactor);

	// Write file length, size factor and filename to start of file data
	uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
	miniFsWriteByte(fs, fileOffset++, (fileTotalLength>>8));
	miniFsWriteByte(fs, fileOffset++, (fileTotalLength&0xFF));
	miniFsWriteByte(fs, fileOffset++, fileSizeFactor);

	miniFsWrite(fs, fileOffset, (const uint8_t *)filename, filenameLen+1);
	fileOffset+=filenameLen+1;

	// Resort the offset list
	miniFsResortFileOffsets(fs);

	assert(miniFsIsConsistent(fs));
	return true;
}

bool miniFsFileDelete(MiniFs *fs, const char *filename) {
	// Is this file system read only?
	if (miniFsGetReadOnly(fs))
		return false;

	// Find index for this filename
	uint8_t index=miniFsFilenameToIndex(fs, filename, NULL);
	if (index==MINIFSMAXFILES)
		return false;

	// Clear header
	miniFsClearFileForIndex(fs, index);

	// Resort the offset list
	miniFsResortFileOffsets(fs);

	assert(miniFsIsConsistent(fs));
	return true;
}

bool miniFsFileResize(MiniFs *fs, const char *filename, uint16_t newContentLen) {
	// Is this file system read only?
	if (miniFsGetReadOnly(fs))
		return false;

	// Find index for this filename
	uint16_t baseOffset;
	uint8_t index=miniFsFilenameToIndex(fs, filename, &baseOffset);
	if (index==MINIFSMAXFILES)
		return false;

	// Not making the file any bigger?
	uint16_t contentLen=miniFsFileGetContentLenFromBaseOffset(fs, baseOffset);
	if (newContentLen<=contentLen) {
		// Reduce stored length
		uint16_t newTotalLen=miniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset)-(contentLen-newContentLen);
		miniFsSetFileTotalLengthForIndex(fs, index, newTotalLen);

		// Reduced stored size
		uint8_t newSizeFactor=miniFsGetSizeFactorForTotalLength(newTotalLen);
		miniFsSetFileSizeFactorForIndex(fs, index, newSizeFactor);

		assert(miniFsIsConsistent(fs));
		return true;
	}

	// Check for enough spare space already so that we can just update the length
	uint16_t delta=newContentLen-contentLen;
	uint16_t totalLen=miniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);
	uint16_t newTotalLen=totalLen+delta;
	uint16_t size=miniFsFileGetSizeFromBaseOffset(fs, baseOffset);
	if (newTotalLen<=size) {
		// Update stored length
		uint16_t fileOffset=miniFsFileGetLengthOffsetFromBaseOffset(fs, baseOffset);
		miniFsWriteByte(fs, fileOffset++, (newTotalLen>>8));
		miniFsWriteByte(fs, fileOffset++, (newTotalLen&0xFF));

		assert(miniFsIsConsistent(fs));
		return true;
	}

	// Check if we have space after us to increase size (and length) sufficiently, without touching anything else
	uint8_t newSizeFactor=(newTotalLen+MINIFSFACTOR-1)/MINIFSFACTOR;
	uint8_t nextFileIndex;
	uint8_t nextFileOffsetFactor;
	for(nextFileIndex=index+1; nextFileIndex<MINIFSMAXFILES; ++nextFileIndex) {
		nextFileOffsetFactor=miniFsFileGetBaseOffsetFactorFromIndex(fs, nextFileIndex);
		if (nextFileOffsetFactor!=0)
			break;
	}

	uint8_t currFileOffsetFactor=miniFsFileGetBaseOffsetFactorFromBaseOffset(fs, baseOffset);
	uint8_t maxSizeIfDesiredFactor;
	if (nextFileIndex!=MINIFSMAXFILES)
		// Calculate maximum size we could be if desired, before hitting this next file
		maxSizeIfDesiredFactor=nextFileOffsetFactor-currFileOffsetFactor;
	else
		// Calculate maximum size we could be if desired, before hitting end of volume
		maxSizeIfDesiredFactor=miniFsGetTotalSizeFactorMinusOne(fs)-currFileOffsetFactor+1;
	if (newSizeFactor<=maxSizeIfDesiredFactor) {
		// We can simply extend in place, update stored length and size
		uint16_t fileLenOffset=miniFsFileGetLengthOffsetFromBaseOffset(fs, baseOffset);
		miniFsWriteByte(fs, fileLenOffset++, (newTotalLen>>8));
		miniFsWriteByte(fs, fileLenOffset++, (newTotalLen&0xFF));
		uint16_t fileSizeFactorOffset=miniFsFileGetSizeFactorOffsetFromBaseOffset(fs, baseOffset);
		miniFsWriteByte(fs, fileSizeFactorOffset++, newSizeFactor);

		assert(miniFsIsConsistent(fs));
		return true;
	}

	// Attempt to simply move to a larger free region
	uint8_t newFileOffsetFactor=miniFsFindFreeRegionFactor(fs, newSizeFactor);
	if (newFileOffsetFactor==0)
		return false;

	// Write file length, size factor and filename to start of new region
	uint16_t fileOffset=((uint16_t)newFileOffsetFactor)*MINIFSFACTOR;
	miniFsWriteByte(fs, fileOffset++, (newTotalLen>>8));
	miniFsWriteByte(fs, fileOffset++, (newTotalLen&0xFF));
	miniFsWriteByte(fs, fileOffset++, newSizeFactor);

	uint16_t filenameLen=strlen(filename);
	miniFsWrite(fs, fileOffset, (const uint8_t *)filename, filenameLen+1);
	fileOffset+=filenameLen+1;

	// Copy file content to new region
	uint16_t oldContentOffset=miniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
	uint16_t oldContentLen=miniFsFileGetContentLenFromIndex(fs, index);
	for(uint16_t i=0; i<oldContentLen; ++i)
		miniFsWriteByte(fs, fileOffset++, miniFsReadByte(fs, oldContentOffset+i));

	// Update file offset factor in header
	miniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+index, newFileOffsetFactor);

	// Resort the offset list
	miniFsResortFileOffsets(fs);

	assert(miniFsIsConsistent(fs));
	return true;
}

uint16_t miniFsFileRead(const MiniFs *fs, const char *filename, uint16_t offset, uint8_t *data, uint16_t len) {
	// Find index for this filename
	uint16_t baseOffset;
	uint8_t index=miniFsFilenameToIndex(fs, filename, &baseOffset);
	if (index==MINIFSMAXFILES)
		return 0;

	// Check offset against length
	uint16_t fileLen=miniFsFileGetContentLenFromBaseOffset(fs, baseOffset);
	if (offset>=fileLen)
		return 0;
	if (len>fileLen-offset)
		len=fileLen-offset;

	// Find position of data
	uint16_t contentOffset=miniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
	if (contentOffset==0)
		return 0;

	// Read data
	return miniFsRead(fs, contentOffset+offset, data, len);
}

uint16_t miniFsFileWrite(MiniFs *fs, const char *filename, uint16_t offset, const uint8_t *data, uint16_t len) {
	// Is this file system read only?
	if (miniFsGetReadOnly(fs))
		return 0;

	// Find index for this filename
	uint16_t baseOffset;
	uint8_t index=miniFsFilenameToIndex(fs, filename, &baseOffset);
	if (index==MINIFSMAXFILES)
		return 0;

	// Check offset against length
	uint16_t fileLen=miniFsFileGetContentLenFromBaseOffset(fs, baseOffset);
	if (offset>=fileLen)
		return false;
	if (len>fileLen-offset)
		len=fileLen-offset;

	// Find content offset
	uint16_t contentOffset=miniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
	if (contentOffset==0)
		return 0;

	// Write byte
	return miniFsWrite(fs, contentOffset+offset, data, len);
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsIsConsistent(const MiniFs *fs) {
	// Verify header
	uint8_t magicByte=miniFsReadByte(fs, MINIFSHEADERMAGICBYTEADDR);
	if (magicByte!=MINIFSHEADERMAGICBYTEVALUE)
		return false;

	uint16_t totalSize=miniFsGetTotalSize(fs);
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return false;

	// Verify files
	uint8_t prevOffsetFactor=0;
	uint8_t i=0;
	while(i<MINIFSMAXFILES) {
		// Grab offset factor from header
		uint8_t fileOffsetFactor=miniFsFileGetBaseOffsetFactorFromIndex(fs, i);
		if (fileOffsetFactor==0)
			break; // end of file list

		// Check the file offset does not overlap the header
		if (fileOffsetFactor<MINIFSFILEMINOFFSETFACTOR)
			return false;

		// Check the file is located after the one in the previous slot.
		if (fileOffsetFactor<prevOffsetFactor)
			return false;

		// Update prev variable for next iteration
		prevOffsetFactor=fileOffsetFactor;
		++i;
	}

	// Verify subsequent header entries are unused
	while(i<MINIFSMAXFILES) {
		// Grab offset factor from header and check if used
		uint8_t fileOffsetFactor=miniFsFileGetBaseOffsetFactorFromIndex(fs, i);
		if (fileOffsetFactor!=0)
			return false;

		++i;
	}

	return true;
}

uint8_t miniFsGetTotalSizeFactorMinusOne(const MiniFs *fs) {
	return miniFsReadByte(fs, MINIFSHEADERTOTALSIZEADDR);
}

uint16_t miniFsRead(const MiniFs *fs, uint16_t addr, uint8_t *data, uint16_t len) {
	return fs->readFunctor(addr, data, len, fs->functorUserData);
}

uint8_t miniFsReadByte(const MiniFs *fs, uint16_t addr) {
	uint8_t value;
	miniFsRead(fs, addr, &value, 1);
	return value;
}

uint16_t miniFsWrite(MiniFs *fs, uint16_t addr, const uint8_t *data, uint16_t len) {
	return fs->writeFunctor(addr, data, len, fs->functorUserData);
}

void miniFsWriteByte(MiniFs *fs, uint16_t addr, uint8_t value) {
	miniFsWrite(fs, addr, &value, 1);
}

bool miniFsGetFilenameFromIndex(const MiniFs *fs, uint8_t index, char filename[MiniFsPathMax]) {
	// Is there not even a file using this slot?
	uint16_t filenameOffset=miniFsFileGetFilenameOffsetFromIndex(fs, index);
	if (filenameOffset==0)
		return false;

	// Copy filename
	char *dest;
	for(dest=filename; dest+1<filename+MiniFsPathMax; dest++, filenameOffset++) {
		uint8_t src=miniFsReadByte(fs, filenameOffset);
		*dest=src;
		if (src=='\0')
			break;
	}

	*dest='\0';

	return true;
}

uint8_t miniFsFilenameToIndex(const MiniFs *fs, const char *filename, uint16_t *baseOffsetPtr) {
	// Loop over all slots looking for the given filename
	for(uint8_t index=0; index<MINIFSMAXFILES; ++index) {
		// Is there even a file using this slot?
		uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
		if (baseOffset==0)
			break; // end of list

		// Check if filename matches
		uint16_t filenameOffset=miniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
		bool match=true;
		for(const char *trueChar=filename; 1; ++trueChar) {
			char testChar=miniFsReadByte(fs, filenameOffset++);
			if (testChar!=*trueChar) {
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

bool miniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index) {
	// Is there even a file in this slot?
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsReadFileInfoFromBaseOffset(fs, info, baseOffset);
}

bool miniFsReadFileInfoFromBaseOffset(const MiniFs *fs, MiniFsFileInfo *info, uint16_t baseOffset) {
	// Is there even a file in this slot?
	info->offset=baseOffset;
	if (info->offset==0)
		return false;

	// Compute total size allocated
	info->size=miniFsFileGetSizeFromBaseOffset(fs, baseOffset);

	// Compute filename length
	info->filenameLen=miniFsFileGetFilenameLenFromBaseOffset(fs, baseOffset);

	// Compute content length
	info->contentLen=miniFsFileGetContentLenFromBaseOffset(fs, baseOffset);

	// Compute spare space
	info->spare=info->size-miniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);

	return true;
}

bool miniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index) {
	return (miniFsReadByte(fs, MINIFSHEADERFILEBASEADDR+index)==MINIFSFILEOFFSETINVALID);
}

uint8_t miniFsGetEmptyIndex(const MiniFs *fs) {
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i)
		if (miniFsIsFileSlotEmpty(fs, i))
			return i;
	return MINIFSMAXFILES;
}

uint8_t miniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor) {
	// No files?
	uint8_t firstFileIndex=0; // due to sorting
	if (miniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex)==0) {
		// Check for insufficent space in volume
		if (sizeFactor>miniFsGetTotalSizeFactorMinusOne(fs)-MINIFSHEADERSIZE/MINIFSFACTOR+1)
			return 0;
		return MINIFSFILEMINOFFSETFACTOR;
	}

	// Check for space before first file.
	if (sizeFactor<=miniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex)-MINIFSFILEMINOFFSETFACTOR)
		return MINIFSFILEMINOFFSETFACTOR;

	// Check for space between files.
	uint8_t secondFileIndex;
	for(secondFileIndex=firstFileIndex+1; secondFileIndex<MINIFSMAXFILES; ++secondFileIndex) {
		// Grab current file's offset and length
		uint8_t secondFileOffsetFactor=miniFsFileGetBaseOffsetFactorFromIndex(fs, secondFileIndex);
		if (secondFileOffsetFactor==0)
			break; // end of file list

		// Calculate space between this file and the last
		uint8_t firstFileOffsetFactor=miniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex);
		uint8_t firstFileSizeFactor=miniFsFileGetSizeFactorFromIndex(fs, firstFileIndex);
		if (sizeFactor<=secondFileOffsetFactor-(firstFileOffsetFactor+firstFileSizeFactor))
			return (firstFileOffsetFactor+firstFileSizeFactor);

		// Prepare for next iteration
		firstFileIndex=secondFileIndex;
	}

	// Check for space after last file.
	uint8_t firstFileOffsetFactor=miniFsFileGetBaseOffsetFactorFromIndex(fs, firstFileIndex);
	uint8_t firstFileSizeFactor=miniFsFileGetSizeFactorFromIndex(fs, firstFileIndex);
	if (sizeFactor<=((uint16_t)miniFsGetTotalSizeFactorMinusOne(fs))+1-(firstFileOffsetFactor+firstFileSizeFactor))
		return (firstFileOffsetFactor+firstFileSizeFactor);

	// No free region large enough
	return 0;
}

uint8_t miniFsFileGetBaseOffsetFactorFromIndex(const MiniFs *fs, uint8_t index) {
	return miniFsReadByte(fs, MINIFSHEADERFILEBASEADDR+index);
}

uint16_t miniFsFileGetBaseOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	return ((uint16_t)miniFsFileGetBaseOffsetFactorFromIndex(fs, index))*MINIFSFACTOR;
}

uint16_t miniFsFileGetLengthOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetLengthOffsetFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetSizeFactorOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetSizeFactorOffsetFromBaseOffset(fs, baseOffset);
}

uint8_t miniFsFileGetSizeFactorFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetSizeFactorFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetSizeFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetSizeFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetFilenameOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetFilenameLenFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetFilenameLenFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetContentOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsFileGetContentLenFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsFileGetContentLenFromBaseOffset(fs, baseOffset);
}

uint16_t miniFsGetFileTotalLengthFromIndex(const MiniFs *fs, uint8_t index) {
	uint16_t baseOffset=miniFsFileGetBaseOffsetFromIndex(fs, index);
	return miniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);
}

uint8_t miniFsFileGetBaseOffsetFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	return baseOffset/MINIFSFACTOR;
}

uint16_t miniFsFileGetLengthOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	return baseOffset;
}

uint16_t miniFsFileGetSizeFactorOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	if (baseOffset==0)
		return 0;
	return baseOffset+2;
}

uint8_t miniFsFileGetSizeFactorFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t sizeFactorOffset=miniFsFileGetSizeFactorOffsetFromBaseOffset(fs, baseOffset);
	if (sizeFactorOffset==0)
		return 0;

	return miniFsReadByte(fs, sizeFactorOffset);
}

uint16_t miniFsFileGetSizeFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t sizeFactor=miniFsFileGetSizeFactorFromBaseOffset(fs, baseOffset);
	if (sizeFactor==0)
		return 0;
	return sizeFactor*MINIFSFACTOR;
}

uint16_t miniFsFileGetFilenameOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	if (baseOffset==0)
		return 0;
	return baseOffset+3;
}

uint16_t miniFsFileGetFilenameLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t filenameOffset=miniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
	uint16_t filenameLen;
	for(filenameLen=0; ; ++filenameLen) {
		uint8_t c=miniFsReadByte(fs, filenameOffset+filenameLen);
		if (c=='\0')
			break;
	}
	return filenameLen;
}

uint16_t miniFsFileGetContentOffsetFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	// Get filename offset and offset by filename length
	uint16_t fileOffset=miniFsFileGetFilenameOffsetFromBaseOffset(fs, baseOffset);
	if (fileOffset==0)
		return 0;
	return fileOffset+miniFsFileGetFilenameLenFromBaseOffset(fs, baseOffset)+1;
}

uint16_t miniFsFileGetContentLenFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t total=miniFsGetFileTotalLengthFromBaseOffset(fs, baseOffset);
	uint16_t offset=miniFsFileGetContentOffsetFromBaseOffset(fs, baseOffset);
	return baseOffset+total-offset;
}

uint16_t miniFsGetFileTotalLengthFromBaseOffset(const MiniFs *fs, uint16_t baseOffset) {
	uint16_t lengthFileOffset=miniFsFileGetLengthOffsetFromBaseOffset(fs, baseOffset);
	if (lengthFileOffset==0)
		return 0;

	uint16_t fileTotalLength=0;
	fileTotalLength|=miniFsReadByte(fs, lengthFileOffset+0);
	fileTotalLength<<=8;
	fileTotalLength|=miniFsReadByte(fs, lengthFileOffset+1);
	return fileTotalLength;
}

void miniFsSetFileTotalLengthForIndex(MiniFs *fs, uint8_t index, uint16_t newTotalLen) {
	uint16_t offset=miniFsFileGetLengthOffsetFromIndex(fs, index);
	assert(offset!=0);
	miniFsWriteByte(fs, offset+0, (newTotalLen>>8));
	miniFsWriteByte(fs, offset+1, (newTotalLen&0xFF));
}

void miniFsSetFileSizeFactorForIndex(MiniFs *fs, uint8_t index, uint8_t newSizeFactor) {
	uint16_t offset=miniFsFileGetSizeFactorOffsetFromIndex(fs, index);
	assert(offset!=0);
	miniFsWriteByte(fs, offset, newSizeFactor);
}

void miniFsClearFileForIndex(MiniFs *fs, uint8_t index) {
	// Clear all file offsets
	miniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+index, MINIFSFILEOFFSETINVALID);
}

uint8_t miniFsGetSizeFactorForTotalLength(uint16_t totalLen) {
	return (totalLen+MINIFSFACTOR-1)/MINIFSFACTOR;
}

void miniFsResortFileOffsets(MiniFs *fs) {
	// Bubble sort - should be fast due to small entry count (MINIFSMAXFILES max but typically much smaller) and being mostly ordered due to last sort

	uint8_t max=MINIFSMAXFILES;

	bool change;
	do {
		change=false;

		for(uint8_t i=1; i<max; ++i) {
			uint8_t factorA=miniFsFileGetBaseOffsetFactorFromIndex(fs, i-1);
			uint8_t factorB=miniFsFileGetBaseOffsetFactorFromIndex(fs, i);
			if (factorB!=0 && (factorA==0 || factorA>factorB)) { // pushes zeros to the end, but otherwise sorts ascending
				// Swap
				change=true;
				miniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+i-1, factorB);
				miniFsWriteByte(fs, MINIFSHEADERFILEBASEADDR+i, factorA);
			}
		}
		--max;
	} while(change);
}
