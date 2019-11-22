#ifndef PTABLE_H
#define PTABLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PTableEntry PTableEntry;

#include "kernelfs.h"

struct PTableEntry {
	uint8_t attributes;
	uint8_t partitionType;
	uint32_t numSectors;
	uint32_t startSector;
};

// In the following functions  n=0,1,2,3 (i.e. 0 indexed), representing partitions 1,2,3,4 (1 indexed)
bool pTableParseEntryPath(const char *path, unsigned n, PTableEntry *entry);
bool pTableParseEntryFd(KernelFsFd fd, unsigned n, PTableEntry *entry);

#endif
