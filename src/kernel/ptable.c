#include "ptable.h"

bool pTableParseEntryPath(const char *path, unsigned n, PTableEntry *entry) {
	// Open disk
	KernelFsFd fd=kernelFsFileOpen(path);
	if (fd==KernelFsFdInvalid)
		return false;

	// Use parse fd version to do most of the work
	bool result=pTableParseEntryFd(fd, n, entry);

	// Close disk
	kernelFsFileClose(fd);

	return result;
}

bool pTableParseEntryFd(KernelFsFd fd, unsigned n, PTableEntry *entry) {
	// Check magic bytes
	uint8_t magicBytes[2];
	if (kernelFsFileReadOffset(fd, 510, magicBytes, 2, false)!=2)
		return false;

	if (magicBytes[0]!=0x55 || magicBytes[1]!=0xAA)
		return false;

	// Read partition table entry
	unsigned offset=446+16*n;
	uint8_t buffer[16];
	if (kernelFsFileReadOffset(fd, offset, buffer, 16, false)!=16)
		return false;

	// Parse entry
	entry->attributes=buffer[0];
	entry->partitionType=buffer[4];
	entry->numSectors=(((uint32_t)buffer[15])<<24)|(((uint32_t)buffer[14])<<16)|(((uint32_t)buffer[13])<<8)|(((uint32_t)buffer[12])<<0);
	entry->startSector=(((uint32_t)buffer[11])<<24)|(((uint32_t)buffer[10])<<16)|(((uint32_t)buffer[9])<<8)|(((uint32_t)buffer[8])<<0);

	return true;
}
