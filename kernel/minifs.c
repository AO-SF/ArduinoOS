#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"
#include "util.h"

#define MINIFSHEADERMAGICBYTEADDR 0
#define MINIFSHEADERMAGICBYTEVALUE 53
#define MINIFSHEADERTOTALSIZEADDR (MINIFSHEADERMAGICBYTEADDR+1)
#define MINIFSHEADERFILEBASEADDR (MINIFSHEADERTOTALSIZEADDR+1)
#define MINIFSHEADERSIZE (1+1+MINIFSMAXFILES) // 64 bytes

#define MINIFSFACTOR 32

#define MINIFSMAXSIZE (MINIFSFACTOR*255) // we use 8 bits to represent the total size (with factor=32 this allows up to 8kb)

#define MINIFSFILEMINOFFSETFACTOR (MINIFSHEADERSIZE/MINIFSFACTOR) // no file can be stored where the header is

typedef struct {
	uint16_t offset;
	uint16_t filenameLen, contentLen;
	uint16_t size, spare;
} MiniFsFileInfo;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint8_t miniFsGetTotalSizeFactor(const MiniFs *fs);

uint8_t miniFsRead(const MiniFs *fs, uint16_t addr);
void miniFsWrite(MiniFs *fs, uint16_t addr, uint8_t value);

bool miniFsGetFilenameFromIndex(const MiniFs *fs, uint8_t index, char filename[MiniFsPathMax]);
uint8_t miniFsFilenameToIndex(const MiniFs *fs, const char *filename); // Returns MINIFSMAXFILES if no such file exists
uint8_t miniFsReadFileOffsetFromIndex(const MiniFs *fs, uint8_t index);
uint16_t miniFsReadFileTotalLengthFromOffsetFactor(const MiniFs *fs, uint8_t fileOffsetFactor);
bool miniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index);
bool miniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index);

uint8_t miniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor, uint8_t *outIndex); // Returns 0 on failure to find

uint16_t miniFsFileHeaderGetContentOffset(const MiniFs *fs, uint8_t fileOffsetFactor);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFormat(MiniFsWriteFunctor *writeFunctor, void *functorUserData, uint16_t maxTotalSize) {
	assert(writeFunctor!=NULL);

	uint16_t totalSizeFactor=maxTotalSize/MINIFSFACTOR;
	uint16_t totalSize=totalSizeFactor*MINIFSFACTOR;

	// Sanity checks
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return false;

	// Write magic number
	writeFunctor(MINIFSHEADERMAGICBYTEADDR, MINIFSHEADERMAGICBYTEVALUE, functorUserData);

	// Write total size
	writeFunctor(MINIFSHEADERTOTALSIZEADDR, totalSizeFactor, functorUserData);

	// Clear file list
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i)
		writeFunctor(MINIFSHEADERFILEBASEADDR+i, 0, functorUserData);

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

	// Verify header
	uint8_t magicByte=miniFsRead(fs, MINIFSHEADERMAGICBYTEADDR);
	if (magicByte!=MINIFSHEADERMAGICBYTEVALUE)
		return false;

	uint16_t totalSize=miniFsGetTotalSize(fs);
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return false;

	uint8_t prevOffsetFactor=0;
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i) {
		// Grab offset factor from header
		uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, i);

		// Does this slot even contain a file?
		if (fileOffsetFactor==0)
			break; // No other slots should have files in

		// Check the file offset does not overlap the header
		if (fileOffsetFactor<MINIFSFILEMINOFFSETFACTOR)
			return false;

		// Check the file is located after the one in the previous slot.
		if (fileOffsetFactor<prevOffsetFactor)
			return false;
		prevOffsetFactor=fileOffsetFactor;
	}

	return true;
}

void miniFsUnmount(MiniFs *fs) {
	// Null-op
}

bool miniFsGetReadOnly(const MiniFs *fs) {
	return (fs->writeFunctor==NULL);
}

uint16_t miniFsGetTotalSize(const MiniFs *fs) {
	return ((uint16_t)miniFsGetTotalSizeFactor(fs))*MINIFSFACTOR;
}

bool miniFsGetChildN(const MiniFs *fs, unsigned childNum, char childPath[MiniFsPathMax]) {
	assert(childNum<MINIFSMAXFILES);

	return miniFsGetFilenameFromIndex(fs, childNum, childPath);
}

uint8_t miniFsGetChildCount(const MiniFs *fs) {
	uint8_t count=0;
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i)
		count+=!miniFsIsFileSlotEmpty(fs, i);
	return count;
}

void miniFsDebug(const MiniFs *fs) {
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
			break;

		// Output info
		printf("		%2i %6i %4i %6i %5i ", i, fileInfo.offset, fileInfo.size, fileInfo.contentLen, fileInfo.spare);
		for(uint16_t filenameOffset=fileInfo.offset+2; ; ++filenameOffset) {
			uint8_t c=miniFsRead(fs, filenameOffset);
			if (c=='\0')
				break;
			printf("%c", c);
		}
		printf("\n");
	}
}

bool miniFsFileExists(const MiniFs *fs, const char *filename) {
	return (miniFsFilenameToIndex(fs, filename)!=MINIFSMAXFILES);
}

uint16_t miniFsFileGetLen(const MiniFs *fs, const char *filename) {
	uint8_t index=miniFsFilenameToIndex(fs, filename);
	if (index==MINIFSMAXFILES)
		return 0;

	MiniFsFileInfo fileInfo;
	if (!miniFsReadFileInfoFromIndex(fs, &fileInfo, index))
		return 0;

	return fileInfo.contentLen;
}

uint16_t miniFsFileGetSize(const MiniFs *fs, const char *filename) {
	// This is the size available for content, not the true size (so does not include the filename)

	uint8_t index=miniFsFilenameToIndex(fs, filename);
	if (index==MINIFSMAXFILES)
		return 0;

	MiniFsFileInfo fileInfo;
	if (!miniFsReadFileInfoFromIndex(fs, &fileInfo, index))
		return 0;

	return fileInfo.contentLen+fileInfo.spare;
}

bool miniFsFileCreate(MiniFs *fs, const char *filename, uint16_t contentSize) {
	uint8_t i;

	// Is filename too long?
	if (strlen(filename)>=MiniFsPathMax)
		return false;

	// We cannot create a file if the FS is read-only
	if (miniFsGetReadOnly(fs))
		return false;

	// File already exists?
	if (miniFsFileExists(fs, filename))
		return false;

	// Look for first empty slot in header we could store the new file in
	uint8_t nextFreeIndex;
	for(nextFreeIndex=0; nextFreeIndex<MINIFSMAXFILES; ++nextFreeIndex) {
		// Is this slot empty?
		uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, nextFreeIndex);
		if (fileOffsetFactor==0)
			break;
	}

	if (nextFreeIndex==MINIFSMAXFILES)
		return false; // No slots in header to store new file

	// Look for large enough region of free space to store the file
	uint16_t fileTotalLength=2+strlen(filename)+1+contentSize;
	uint8_t fileSizeFactor=(utilNextPow2(fileTotalLength)+MINIFSFACTOR-1)/MINIFSFACTOR;

	uint8_t newIndex;
	uint8_t fileOffsetFactor=miniFsFindFreeRegionFactor(fs, fileSizeFactor, &newIndex);
	if (fileOffsetFactor==0)
		return false;

	// Insert file offset and length factors into header in order, after shifting others up.
	for(i=MINIFSMAXFILES-1; i>newIndex; --i)
		miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+i, miniFsRead(fs, MINIFSHEADERFILEBASEADDR+i-1));

	miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+newIndex, fileOffsetFactor);

	// Write file length and filename to start of file data
	uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
	miniFsWrite(fs, fileOffset++, (fileTotalLength>>8));
	miniFsWrite(fs, fileOffset++, (fileTotalLength&0xFF));
	i=0;
	do {
		miniFsWrite(fs, fileOffset++, filename[i]);
	} while(filename[i++]!='\0');

	return true;
}

bool miniFsFileDelete(MiniFs *fs, const char *filename) {
	// Is this file system read only?
	if (miniFsGetReadOnly(fs))
		return false;

	// Find index for this filename
	uint8_t index=miniFsFilenameToIndex(fs, filename);
	if (index==MINIFSMAXFILES)
		return false;

	// Write 0 offset into header at found index
	miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+index, 0);

	return true;
}

int miniFsFileRead(const MiniFs *fs, const char *filename, uint16_t offset) {
	// Find index for this filename
	uint8_t index=miniFsFilenameToIndex(fs, filename);
	if (index==MINIFSMAXFILES)
		return -1;

	// Check offset against length
	if (offset>=miniFsFileGetLen(fs, filename))
		return -1;

	// Read byte
	uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, index);
	uint16_t contentOffset=miniFsFileHeaderGetContentOffset(fs, fileOffsetFactor);
	return miniFsRead(fs, contentOffset+offset);
}

bool miniFsFileWrite(MiniFs *fs, const char *filename, uint16_t offset, uint8_t value) {
	// Is this file system read only?
	if (miniFsGetReadOnly(fs))
		return false;

	// Find index for this filename
	uint8_t index=miniFsFilenameToIndex(fs, filename);
	if (index==MINIFSMAXFILES)
		return false;

	// Check offset against length
	if (offset>=miniFsFileGetLen(fs, filename))
		return false;

	// Write byte
	uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, index);
	uint16_t contentOffset=miniFsFileHeaderGetContentOffset(fs, fileOffsetFactor);
	miniFsWrite(fs, contentOffset+offset, value);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint8_t miniFsGetTotalSizeFactor(const MiniFs *fs) {
	return miniFsRead(fs, MINIFSHEADERTOTALSIZEADDR);
}

uint8_t miniFsRead(const MiniFs *fs, uint16_t addr) {
	return fs->readFunctor(addr, fs->functorUserData);
}

void miniFsWrite(MiniFs *fs, uint16_t addr, uint8_t value) {
	uint8_t originalValue=miniFsRead(fs, addr);
	if (value!=originalValue)
		fs->writeFunctor(addr, value, fs->functorUserData);
}

bool miniFsGetFilenameFromIndex(const MiniFs *fs, uint8_t index, char filename[MiniFsPathMax]) {
	// Parse header
	uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, index);

	// Is there not even a file using this slot?
	if (fileOffsetFactor==0)
		return false;

	// Copy filename
	uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
	fileOffset+=2;
	char *dest;
	for(dest=filename; dest+1<filename+MiniFsPathMax; dest++, fileOffset++) {
		uint8_t src=miniFsRead(fs, fileOffset);
		*dest=src;
		if (src=='\0')
			break;
	}

	*dest='\0';

	return true;
}

uint8_t miniFsFilenameToIndex(const MiniFs *fs, const char *filename) {
	// Loop over all slots looking for the given filename
	for(uint8_t index=0; index<MINIFSMAXFILES; ++index) {
		// Parse header
		uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, index);

		// Is there even a file using this slot?
		if (fileOffsetFactor==0)
			continue;

		// Check filename matches
		uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
		fileOffset+=2;
		bool match=true;
		for(const char *trueChar=filename; 1; ++trueChar) {
			char testChar=miniFsRead(fs, fileOffset++);
			if (testChar!=*trueChar) {
				match=false;
				break;
			}
			if (testChar=='\0')
				break;
		}

		if (match)
			return index;
	}

	return MINIFSMAXFILES;
}

uint8_t miniFsReadFileOffsetFromIndex(const MiniFs *fs, uint8_t index) {
	return miniFsRead(fs, MINIFSHEADERFILEBASEADDR+index);
}

uint16_t miniFsReadFileTotalLengthFromOffsetFactor(const MiniFs *fs, uint8_t fileOffsetFactor) {
	uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
	uint16_t fileTotalLength=miniFsRead(fs, fileOffset+0);
	fileTotalLength<<=8;
	fileTotalLength|=miniFsRead(fs, fileOffset+1);
	return fileTotalLength;
}

bool miniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index) {
	// Is there even a file in this slot?
	uint8_t fileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, index);
	if (fileOffsetFactor==0)
		return false;
	info->offset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;

	// Grab length
	uint16_t fileTotalLength=miniFsReadFileTotalLengthFromOffsetFactor(fs, fileOffsetFactor);

	// Compute total size allocated
	uint8_t fileSizeFactor=(utilNextPow2(fileTotalLength)+MINIFSFACTOR-1)/MINIFSFACTOR;
	info->size=((uint16_t)fileSizeFactor)*MINIFSFACTOR;

	// Compute filename length
	for(info->filenameLen=info->offset+2; 1; ++info->filenameLen) {
		uint8_t c=miniFsRead(fs, info->filenameLen);
		if (c=='\0')
			break;
	}
	info->filenameLen-=info->offset+2;

	// Compute content length
	info->contentLen=fileTotalLength-(info->filenameLen+1)-2;

	// Compute spare space
	info->spare=info->size-fileTotalLength;

	return true;
}

bool miniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index) {
	return (miniFsRead(fs, MINIFSHEADERFILEBASEADDR+index)==0);
}

uint8_t miniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor, uint8_t *outIndex) {
	// No files yet?
	uint8_t firstFileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, 0);
	if (firstFileOffsetFactor==0) {
		if (1) { // TODO: Check we have enough space in the volume to allow this
			*outIndex=0;
			return MINIFSFILEMINOFFSETFACTOR;
		} else
			return 0;
	}

	// Check for space before first file.
	if (sizeFactor<=firstFileOffsetFactor-MINIFSFILEMINOFFSETFACTOR) {
		*outIndex=0;
		return MINIFSFILEMINOFFSETFACTOR;
	}

	// Grab first files total length
	uint16_t firstFileTotalLength=miniFsReadFileTotalLengthFromOffsetFactor(fs, firstFileOffsetFactor);

	// Check for space between files.
	uint8_t i;
	for(i=1; i<MINIFSMAXFILES; ++i) {
		// Grab current file's offset and length
		uint8_t secondFileOffsetFactor=miniFsReadFileOffsetFromIndex(fs, i);
		if (secondFileOffsetFactor==0)
			break; // No more files

		// Calculate space between.
		uint16_t firstFileSize=utilNextPow2(firstFileTotalLength);
		uint8_t firstFileSizeFactor=(firstFileSize+MINIFSFACTOR-1)/MINIFSFACTOR;
		if (sizeFactor<=secondFileOffsetFactor-(firstFileOffsetFactor+firstFileSizeFactor)) {
			*outIndex=i;
			return (firstFileOffsetFactor+firstFileSizeFactor);
		}

		// Prepare for next iteration
		firstFileOffsetFactor=secondFileOffsetFactor;
		uint16_t secondFileTotalLength=miniFsReadFileTotalLengthFromOffsetFactor(fs, secondFileOffsetFactor);
		firstFileTotalLength=secondFileTotalLength;
	}

	// No spare slots anyway?
	if (i==MINIFSMAXFILES)
		return 0;

	// Check for space after last file.
	uint16_t firstFileSizeMin=utilNextPow2(firstFileTotalLength);
	uint8_t firstFileSizeFactor=(firstFileSizeMin+MINIFSFACTOR-1)/MINIFSFACTOR;
	if (sizeFactor<=miniFsGetTotalSizeFactor(fs)-(firstFileOffsetFactor+firstFileSizeFactor)) {
		*outIndex=i;
		return (firstFileOffsetFactor+firstFileSizeFactor);
	}

	// No free region large enough
	return 0;
}

uint16_t miniFsFileHeaderGetContentOffset(const MiniFs *fs, uint8_t fileOffsetFactor) {
	// Skip past length and name
	uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
	fileOffset+=2;
	while(1) {
		uint8_t c=miniFsRead(fs, fileOffset++);
		if (c=='\0')
			return fileOffset;
	}
	return 0;
}
