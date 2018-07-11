#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"
#include "util.h"

#define MINIFSHEADERMAGICBYTEADDR 0
#define MINIFSHEADERMAGICBYTEVALUE 53
#define MINIFSHEADERTOTALSIZEADDR (MINIFSHEADERMAGICBYTEADDR+1)
#define MINIFSHEADERFILEBASEADDR (MINIFSHEADERTOTALSIZEADDR+1)
#define MINIFSHEADERSIZE (1+1+2*MINIFSMAXFILES) // 128 bytes

#define MINIFSFACTOR 32

#define MINIFSMAXSIZE (MINIFSFACTOR*255) // we use 8 bits to represent the total size (with factor=32 this allows up to 8kb)

#define MINIFSFILEMINOFFSETFACTOR (MINIFSHEADERSIZE/MINIFSFACTOR) // no file can be stored where the header is

#define MiniFsFileLengthBits 10
#define MiniFsFileLengthMax (((uint16_t)(1u))<<MiniFsFileLengthBits)

typedef struct {
	union {
		struct {
			uint16_t upper:8;
			uint16_t lower:8;
		};
		struct {
			uint16_t offsetFactor:6;
			uint16_t totalLength:MiniFsFileLengthBits;
		};
	};
} MiniFsFileHeader;

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

uint8_t miniFsFilenameToIndex(const MiniFs *fs, const char *filename); // Returns MINIFSMAXFILES if no such file exists
uint8_t miniFsFileDescriptorToIndex(const MiniFs *fs, MiniFsFileDescriptor fd); // Returns MINIFSMAXFILES if no such file exists
MiniFsFileHeader miniFsReadFileHeaderFromIndex(const MiniFs *fs, uint8_t index);
bool miniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index);
bool miniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index);

uint8_t miniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor, uint8_t *outIndex); // Returns 0 on failure to find

bool miniFsOpenBitsetGet(const MiniFs *fs, uint8_t index);
void miniFsOpenBitsetSet(MiniFs *fs, uint8_t index, bool open);

uint8_t miniFsFileCreate(MiniFs *fs, const char *filename, uint16_t contentLen); // Returns file index on success, MINIFSMAXFILES on failure. Does NOT check if the filename already exists.

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool miniFsFormat(MiniFsWriteFunctor *writeFunctor, uint16_t maxTotalSize) {
	uint16_t totalSizeFactor=maxTotalSize/MINIFSFACTOR;
	uint16_t totalSize=totalSizeFactor*MINIFSFACTOR;

	// Sanity checks
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return false;

	// Write magic number
	writeFunctor(MINIFSHEADERMAGICBYTEADDR, MINIFSHEADERMAGICBYTEVALUE);

	// Write total size
	writeFunctor(MINIFSHEADERTOTALSIZEADDR, totalSizeFactor);

	// Clear file list
	MiniFsFileHeader nullFileHeader={.offsetFactor=0, .totalLength=0};
	writeFunctor(MINIFSHEADERFILEBASEADDR+2*0+0, nullFileHeader.upper);
	writeFunctor(MINIFSHEADERFILEBASEADDR+2*0+1, nullFileHeader.lower);

	return true;
}

bool miniFsMountFast(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor) {
	// Simply copy IO functors and clear open bitset
	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	for(uint8_t i=0; i<(MINIFSMAXFILES+7)/8; ++i)
		fs->openBitset[i]=0;

	return true;
}

bool miniFsMountSafe(MiniFs *fs, MiniFsReadFunctor *readFunctor, MiniFsWriteFunctor *writeFunctor) {
	// Call fast function to do basic initialisation
	if (!miniFsMountFast(fs, readFunctor, writeFunctor))
		return false;

	// Verify header
	// TODO: Either give better errors on failure (such as bad magic byte), or allow mounting anyway but mention as warnings (such as individually courrupted files).
	uint8_t magicByte=miniFsRead(fs, MINIFSHEADERMAGICBYTEADDR);
	if (magicByte!=MINIFSHEADERMAGICBYTEVALUE)
		return false;

	uint16_t totalSize=miniFsGetTotalSize(fs);
	if (totalSize<MINIFSHEADERSIZE || totalSize>MINIFSMAXSIZE)
		return false;

	uint8_t prevOffsetFactor=0;
	for(uint8_t i=0; i<MINIFSMAXFILES; ++i) {
		// Parse header
		MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, i);

		// Does this slot even contain a file?
		if (fileHeader.offsetFactor==0)
			break; // No other slots should have files in

		// Check the file offset does not overlap the header or exceed the size of the volume.
		// TODO: Check for exceeding volume size.
		if (fileHeader.offsetFactor<MINIFSFILEMINOFFSETFACTOR)
			return false;

		// Check the file is located after the one in the previous slot.
		if (fileHeader.offsetFactor<prevOffsetFactor)
			return false;
		prevOffsetFactor=fileHeader.offsetFactor;

		// TODO: Checks on file length and total length/size.
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

void miniFsDebug(const MiniFs *fs) {
	printf("Volume debug:\n");
	printf("	max total size: %u bytes\n", miniFsGetTotalSize(fs));
	printf("	header size: %u bytes (%u%% of total, leaving %u bytes for file data)\n", MINIFSHEADERSIZE, (100*MINIFSHEADERSIZE)/miniFsGetTotalSize(fs), miniFsGetTotalSize(fs)-MINIFSHEADERSIZE);
	printf("	mount mode: %s\n", (miniFsGetReadOnly(fs) ? "RO" : "RW"));
	printf("	files:\n");
	printf("		ID OFFSET SIZE LENGTH OPEN SPARE FILENAME\n");

	for(uint8_t i=0; i<MINIFSMAXFILES; ++i) {
		// Parse header
		MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, i);
		if (fileHeader.offsetFactor==0)
			break;

		MiniFsFileInfo fileInfo;
		if (!miniFsReadFileInfoFromIndex(fs, &fileInfo, i))
			break;

		// Output info
		bool isOpen=miniFsOpenBitsetGet(fs, i);
		printf("		%2i %6i %4i %6i %4i %5i ", i, fileInfo.offset, fileInfo.size, fileInfo.contentLen, isOpen, fileInfo.spare);
		for(uint16_t filenameOffset=fileInfo.offset; ; ++filenameOffset) {
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

MiniFsFileDescriptor miniFsFileOpenRO(const MiniFs *fs, const char *filename) {
	return MiniFsFileDescriptorNone; // TODO: this after we finish miniFsFileOpenRW
}

MiniFsFileDescriptor miniFsFileOpenRW(MiniFs *fs, const char *filename, bool create) {
	// Sanity checks
	if (filename==NULL || filename[0]=='\0')
		return MiniFsFileDescriptorNone;

	// We cannot open a file for writing if the FS is read-only
	if (miniFsGetReadOnly(fs))
		return MiniFsFileDescriptorNone;

	// Check if the file does not exist
	if (!miniFsFileExists(fs, filename)) {
		// Not allowed to create it?
		if (!create)
			return MiniFsFileDescriptorNone;

		// Attempt to create new file.
		uint8_t index=miniFsFileCreate(fs, filename, 0);
		if (index==MINIFSMAXFILES)
			return MiniFsFileDescriptorNone;
	}

	// Check this file is not open already, and if not, mark it so
	uint8_t index=miniFsFilenameToIndex(fs, filename);
	if (miniFsOpenBitsetGet(fs, index))
		return false;

	miniFsOpenBitsetSet(fs, index, true);

	// Compute the file descriptor (essentially just the file's offset)
	MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, index);
	MiniFsFileDescriptor fd=fileHeader.offsetFactor;

	return fd;
}

void miniFsFileClose(MiniFs *fs, MiniFsFileDescriptor fd) {
	// Clear open flag
	uint8_t index=miniFsFileDescriptorToIndex(fs, fd);
	miniFsOpenBitsetSet(fs, index, false);
}

MiniFsFileDescriptor miniFsFileResize(MiniFs *fs, MiniFsFileDescriptor fd, uint16_t newLen) {
	/*

	.....

	if newLen is less than or equal to existing length available (so spare+usedlen, or totalsize-filenamesize)
		nothing really needs doing - simply update length field in header
	if however newLen is greater than available length
		check first if there is enough free space after the file
			if so, we can simply increase the length in the header so that the size is automatically updated
		otherwise we will need to check if there is a new region we could move the file to
		and if so, update header and file descriptor etc

		in this case might be easier initially to simply create a whole new file, copying the old one over, before deleting the original

	*/

	// Check the file even exists (and if it does, grab its info)
	uint8_t index=miniFsFileDescriptorToIndex(fs, fd);
	if (index==MINIFSMAXFILES)
		return 0;

	MiniFsFileInfo fileInfo;
	miniFsReadFileInfoFromIndex(fs, &fileInfo, index);

	// Do we already have enough spare space to avoid having to reallocate?
	uint16_t maxLen=fileInfo.size-(fileInfo.filenameLen+1);
	if (newLen<=maxLen) {
		// Simply update stored length
		MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, index);
		fileHeader.totalLength=(fileInfo.filenameLen+1)+newLen;
		miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+2*index+0, fileHeader.upper);
		miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+2*index+1, fileHeader.lower);

		return fd;
	}

	// Otherwise look for a new region to move the file to.
	// .....

	/*

	.....

	this is a little tricky
	ideally we would use miniFsFileCreate to create a new file (sending contentLen=newLen so it is the correct size)
	then copy over the data, before deleting the original (being careful regarding file descriptors, indexes, open bits, etc)

	only snag is passing a const char * as the filename argument to create the new file
	perhaps make a version of create which can read from the eeprom at a given address? this is difficult

	//uint8_t newIndex=miniFsFileCreate(fs, const char *filename, uint16_t contentLen); // Returns file index on success, MINIFSMAXFILES on failure. Does NOT check if the filename already exists.

	*/

	return fd; // .....
}

	/*
uint16_t miniFsFileRead(const MiniFs *fs, MiniFsFileDescriptor fd, uint16_t offset, uint8_t *data, uint16_t dataLen) {

	.....

	"Attempts to read up to dataLen number of items from the file at position offset. Returns the number of items successfully read."


	return 0; // .....
}
	*/

/*
uint16_t miniFsFileWrite(MiniFs *fs, MiniFsFileDescriptor fd, uint16_t offset, const uint8_t *data, uint16_t dataLen) {
	// Check the file even exists (and if it does, grab its info)
	uint8_t index=miniFsFileDescriptorToIndex(fs, fd);
	if (index==MINIFSMAXFILES)
		return 0;

	MiniFsFileInfo fileInfo;
	miniFsReadFileInfoFromIndex(fs, &fileInfo, index);

	// Check that offset is not beyond the end of the file.
	if (offset>fileInfo.contentLen)
		return 0;

	// Calculate length content requires if we do manage to write all bytes
	uint16_t newContentLenMax=offset+dataLen;
	if (newContentLenMax<fileInfo.contentLen)
		newContentLenMax=fileInfo.contentLen;

	// Attempt to make file larger if we need to.
	if (newContentLenMax-fileInfo.contentLen<fileInfo.spare) {
		.....
		/*

		.....

		so we need to look first to see if we already have enough spare space
			if so, we can update length in header and be done
		otherwise we need to look if there is a free region large enough to move the file to
			if so, we need to update both offsetfactor and length in the header
			file descriptors are also wrong at this point?

		/
	}

	// ..... at this point need to refresh fileInfo and also consider case where we can already use the spare - when is length updated


.....

THIS IS ALL QUITE COMPLICATED
what if instead we have a resize function which changes the length of a file (truncating or extending)
and also a write function which writes a single byte, but only within the existing files boundaries
then go from there


	// Update data
	/*
	.....
	uint16_t fileOffset=?;
	for(uint16_t i=0; i<dataLen; ++i) {
		..... check bounds of file size - we may not have managed to allocated enough above
		miniFsWrite(fs, fileOffset+....skipname+offset+i, data[i]);
	}

	return .....;
	return 0; // .....
}
	*/

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint8_t miniFsGetTotalSizeFactor(const MiniFs *fs) {
	return miniFsRead(fs, MINIFSHEADERTOTALSIZEADDR);
}

uint8_t miniFsRead(const MiniFs *fs, uint16_t addr) {
	return fs->readFunctor(addr);
}

void miniFsWrite(MiniFs *fs, uint16_t addr, uint8_t value) {
	uint8_t originalValue=miniFsRead(fs, addr);
	if (value!=originalValue)
		fs->writeFunctor(addr, value);
}

uint8_t miniFsFilenameToIndex(const MiniFs *fs, const char *filename) {
	// Loop over all slots looking for the given filename
	for(uint8_t index=0; index<MINIFSMAXFILES; ++index) {
		// Parse header
		MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, index);

		// Is there even a file using this slot?
		if (fileHeader.offsetFactor==0)
			continue;

		// Check filename matches
		uint16_t fileOffset=((uint16_t)fileHeader.offsetFactor)*MINIFSFACTOR;
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

uint8_t miniFsFileDescriptorToIndex(const MiniFs *fs, MiniFsFileDescriptor fd) {
	// Loop over all slots looking for the given file descriptor
	for(uint8_t index=0; index<MINIFSMAXFILES; ++index) {
		// Parse header
		MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, index);

		// Match?
		if (fileHeader.offsetFactor==fd)
			return index;

		// End of files?
		if (fileHeader.offsetFactor==0)
			break;
	}

	return MINIFSMAXFILES;
}

MiniFsFileHeader miniFsReadFileHeaderFromIndex(const MiniFs *fs, uint8_t index) {
	MiniFsFileHeader fileHeader;
	fileHeader.upper=miniFsRead(fs, MINIFSHEADERFILEBASEADDR+2*index+0);
	fileHeader.lower=miniFsRead(fs, MINIFSHEADERFILEBASEADDR+2*index+1);
	return fileHeader;
}

bool miniFsReadFileInfoFromIndex(const MiniFs *fs, MiniFsFileInfo *info, uint8_t index) {
	// Is there even a file in this slot?
	MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, index);
	if (fileHeader.offsetFactor==0)
		return false;

	// Compute offset and total size allocated
	info->offset=((uint16_t)fileHeader.offsetFactor)*MINIFSFACTOR;
	uint8_t fileSizeFactor=(utilNextPow2(fileHeader.totalLength)+MINIFSFACTOR-1)/MINIFSFACTOR;
	info->size=((uint16_t)fileSizeFactor)*MINIFSFACTOR;

	// Compute filename length
	for(info->filenameLen=info->offset; 1; ++info->filenameLen) {
		uint8_t c=miniFsRead(fs, info->filenameLen);
		if (c=='\0')
			break;
	}
	info->filenameLen-=info->offset;

	// Compute content length
	info->contentLen=fileHeader.totalLength-(info->filenameLen+1);

	// Compute spare space
	info->spare=info->size-fileHeader.totalLength;

	return true;
}

bool miniFsIsFileSlotEmpty(const MiniFs *fs, uint8_t index) {
	return (miniFsRead(fs, MINIFSHEADERFILEBASEADDR+2*index+0)==0);
}

uint8_t miniFsFindFreeRegionFactor(const MiniFs *fs, uint8_t sizeFactor, uint8_t *outIndex) {
	// No files yet?
	MiniFsFileHeader firstFileHeader=miniFsReadFileHeaderFromIndex(fs, 0);
	if (firstFileHeader.offsetFactor==0) {
		if (1) { // TODO: Check we have enough space in the volume to allow this
			*outIndex=0;
			return MINIFSFILEMINOFFSETFACTOR;
		} else
			return 0;
	}

	// Check for space before first file.
	if (sizeFactor<=firstFileHeader.offsetFactor-MINIFSFILEMINOFFSETFACTOR) {
		*outIndex=0;
		return MINIFSFILEMINOFFSETFACTOR;
	}

	// Check for space between files.
	uint8_t i;
	for(i=1; i<MINIFSMAXFILES; ++i) {
		// Grab current file's offset and length
		MiniFsFileHeader secondFileHeader=miniFsReadFileHeaderFromIndex(fs, i);
		if (secondFileHeader.offsetFactor==0)
			break; // No more files

		// Calculate space between.
		uint16_t firstFileSize=utilNextPow2(firstFileHeader.totalLength)*MINIFSFACTOR;
		uint8_t firstFileSizeFactor=(firstFileSize+MINIFSFACTOR-1)/MINIFSFACTOR;
		if (sizeFactor<=secondFileHeader.offsetFactor-(firstFileHeader.offsetFactor+firstFileSizeFactor)) {
			*outIndex=i;
			return (firstFileHeader.offsetFactor+firstFileSizeFactor);
		}

		// Prepare for next iteration
		firstFileHeader=secondFileHeader;
	}

	// No spare slots anyway?
	if (i==MINIFSMAXFILES)
		return 0;

	// Check for space after last file.
	uint16_t firstFileSizeMin=utilNextPow2(firstFileHeader.totalLength);
	uint8_t firstFileSizeFactor=(firstFileSizeMin+MINIFSFACTOR-1)/MINIFSFACTOR;
	if (sizeFactor<=miniFsGetTotalSizeFactor(fs)-(firstFileHeader.offsetFactor+firstFileSizeFactor)) {
		*outIndex=i;
		return (firstFileHeader.offsetFactor+firstFileSizeFactor);
	}

	// No free region large enough
	return 0;
}

bool miniFsOpenBitsetGet(const MiniFs *fs, uint8_t index) {
	uint8_t openBitsetIndex=index/8;
	uint8_t openBitsetShift=index%8;
	return ((fs->openBitset[openBitsetIndex]>>openBitsetShift)&1);
}

void miniFsOpenBitsetSet(MiniFs *fs, uint8_t index, bool open) {
	uint8_t openBitsetIndex=index/8;
	uint8_t openBitsetShift=index%8;
	uint8_t openBitSetMask=((1u)<<openBitsetShift);

	if (open)
		fs->openBitset[openBitsetIndex]|=openBitSetMask;
	else
		fs->openBitset[openBitsetIndex]&=~openBitSetMask;
}

uint8_t miniFsFileCreate(MiniFs *fs, const char *filename, uint16_t contentLen) {
	uint8_t i;

	// We cannot create a file if the FS is read-only
	if (miniFsGetReadOnly(fs))
		return MINIFSMAXFILES;

	// Look for first empty slot in header we could store the new file in
	uint8_t nextFreeIndex;
	for(nextFreeIndex=0; nextFreeIndex<MINIFSMAXFILES; ++nextFreeIndex) {
		// Is this slot empty?
		MiniFsFileHeader fileHeader=miniFsReadFileHeaderFromIndex(fs, nextFreeIndex);
		if (fileHeader.offsetFactor==0)
			break;
	}

	if (nextFreeIndex==MINIFSMAXFILES)
		return MINIFSMAXFILES; // No slots in header to store new file

	// Look for large enough region of free space to store the file
	uint16_t fileLength=strlen(filename)+1+contentLen;
	uint8_t fileSizeFactor=(utilNextPow2(fileLength)+MINIFSFACTOR-1)/MINIFSFACTOR;

	uint8_t newIndex;
	uint8_t fileOffsetFactor=miniFsFindFreeRegionFactor(fs, fileSizeFactor, &newIndex);
	if (fileOffsetFactor==0)
		return MINIFSMAXFILES;

	// Insert file offset and length factors into header in order, after shifting others up.
	for(i=MINIFSMAXFILES-1; i>newIndex; --i) {
		miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+2*i+0, miniFsRead(fs, MINIFSHEADERFILEBASEADDR+2*(i-1)+0));
		miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+2*i+1, miniFsRead(fs, MINIFSHEADERFILEBASEADDR+2*(i-1)+1));
		miniFsOpenBitsetSet(fs, i, miniFsOpenBitsetGet(fs, i-1));
	}

	MiniFsFileHeader newFileHeader={.offsetFactor=fileOffsetFactor, .totalLength=fileLength};
	miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+2*newIndex+0, newFileHeader.upper);
	miniFsWrite(fs, MINIFSHEADERFILEBASEADDR+2*newIndex+1, newFileHeader.lower);
	miniFsOpenBitsetSet(fs, newIndex, 0);

	// Write filename to start of file data
	uint16_t fileOffset=((uint16_t)fileOffsetFactor)*MINIFSFACTOR;
	i=0;
	do {
		miniFsWrite(fs, fileOffset+i, filename[i]);
	} while(filename[i++]!='\0');

	return newIndex;
}
