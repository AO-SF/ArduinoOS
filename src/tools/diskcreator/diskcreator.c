#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	uint8_t attributes;
	uint8_t type;
	uint32_t numSectors;
} PTableEntry;

typedef struct {
	PTableEntry entries[4];
} PTable;

void pTableInit(PTable *pTable);
void pTableSetEntry(PTable *pTable, unsigned partitionIndex, uint8_t attributes, uint8_t type, uint32_t numSectors);
bool pTableWrite(const PTable *pTable, FILE *file, const char *partitionPaths[4]); // writes first 512 byte sector
bool pTableWriteEntry(const PTable *pTable, FILE *file, unsigned partitionIndex, const char *partitionPath); // writes partition padded to multiple of 512 bytes

int main(int argc, char **argv) {
	// Parse arguments
	if (argc<2 || argc>6) {
		printf("usage: %s outputpath [partition1image [partition2image [partition3image [partition4image]]]]\n", argv[0]);
		return 0;
	}

	const char *outPath=argv[1];
	const char *partitionPaths[4];
	for(unsigned i=0; i<4; ++i)
		partitionPaths[i]=(argc>i+2 ? argv[i+2] : NULL);

	// Create partition table structure
	PTable pTable;
	pTableInit(&pTable);

	for(unsigned i=0; i<4; ++i) {
		// Empty partition entry?
		if (partitionPaths[i]==NULL)
			continue;

		// Read partition size
		FILE *partitionFile=fopen(partitionPaths[i], "r");
		if (partitionFile==NULL) {
			printf("Warning: Could not open partition file '%s' for reading\n", partitionPaths[i]);
			continue;
		}

		fseek(partitionFile, 0L, SEEK_END);
		uint64_t size=ftell(partitionFile);
		rewind(partitionFile);

		fclose(partitionFile);

		// Set table entry
		uint32_t numSectors=(size+511)/512;
		pTableSetEntry(&pTable, i, 0x00, 0x7F, numSectors);
	}

	// Write output file
	FILE *outFile=fopen(outPath, "w");
	if (outFile==NULL) {
		printf("Could not open output path '%s' for writing\n", outPath);
		return 1;
	}

	if (!pTableWrite(&pTable, outFile, partitionPaths))
		return 1;

	fclose(outFile);

	return 0;
}

void pTableInit(PTable *pTable) {
	for(unsigned i=0; i<4; ++i) {
		pTable->entries[i].attributes=0;
		pTable->entries[i].type=0;
		pTable->entries[i].numSectors=0;
	}
}

void pTableSetEntry(PTable *pTable, unsigned partitionIndex, uint8_t attributes, uint8_t type, uint32_t numSectors) {
	assert(partitionIndex<4);

	pTable->entries[partitionIndex].attributes=attributes;
	pTable->entries[partitionIndex].type=type;
	pTable->entries[partitionIndex].numSectors=numSectors;
}

bool pTableWrite(const PTable *pTable, FILE *file, const char *partitionPaths[4]) {
	// Create 512-byte buffer
	char buffer[512];
	memset(buffer, 0, 512);

	// 2 magic bytes
	buffer[510]=0x55;
	buffer[511]=0xAA;

	// partition entries
	uint32_t nextStartSector=1;
	for(unsigned i=0; i<4; ++i) {
		uint32_t startSector=(pTable->entries[i].numSectors>0 ? nextStartSector : 0);
		nextStartSector+=pTable->entries[i].numSectors;

		unsigned offset=446+16*i;
		buffer[offset]=pTable->entries[i].attributes;
		buffer[offset+4]=pTable->entries[i].type;
		buffer[offset+8]=(startSector>>0)&0xFF;
		buffer[offset+9]=(startSector>>8)&0xFF;
		buffer[offset+10]=(startSector>>16)&0xFF;
		buffer[offset+11]=(startSector>>24)&0xFF;
		buffer[offset+12]=(pTable->entries[i].numSectors>>0)&0xFF;
		buffer[offset+13]=(pTable->entries[i].numSectors>>8)&0xFF;
		buffer[offset+14]=(pTable->entries[i].numSectors>>16)&0xFF;
		buffer[offset+15]=(pTable->entries[i].numSectors>>24)&0xFF;
	}

	// Write out data
	if (fwrite(buffer, 1, 512, file)!=512) {
		printf("Could not write out partition table\n");
		return false;
	}

	// Write out each partition
	for(unsigned i=0; i<4; ++i) {
		if (!pTableWriteEntry(pTable, file, i, partitionPaths[i])) {
			printf("Could not write partition %u ('%s')\n", i+1, partitionPaths[i]);
			return false;
		}
	}

	return true;
}

bool pTableWriteEntry(const PTable *pTable, FILE *file, unsigned partitionIndex, const char *partitionPath) {
	// Empty partition?
	if (pTable->entries[partitionIndex].numSectors==0)
		return true;

	// Write file data in 512 byte chunks
	FILE *partitionFile=fopen(partitionPath, "r");
	if (partitionFile==NULL)
		return false;

	for(uint32_t sector=0; sector<pTable->entries[partitionIndex].numSectors; ++sector) {
		uint8_t buffer[512];
		memset(buffer, 0xFF, 512);

		fread(buffer, 1, 512, partitionFile); // TODO: should do some return value checking, but we don't always get 512 bytes in final sector
		if (fwrite(buffer, 1, 512, file)!=512)
			goto error;
	}

	fclose(partitionFile);

	return true;

	error:
	fclose(partitionFile);
	return false;
}
