#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "minifsextra.h"

bool miniFsExtraAddFile(MiniFs *fs, const char *destPath, const char *srcPath, bool verbose) {
	// Open src file for reading
	FILE *file=fopen(srcPath, "r");
	if (file==NULL) {
		if (verbose)
			printf("warning unable to open file '%s' for reading\n", srcPath);
		return false;
	}

	// Create file in minifs volume to represent this onernel.c:
	fseek(file, 0L, SEEK_END);
	uint16_t fileSize=ftell(file);
	if (!miniFsFileCreate(fs, destPath, fileSize)) {
		if (verbose)
			printf("warning unable to create file '%s' representing '%s' (size=%u)\n", destPath, srcPath, fileSize);
		fclose(file);
		return false;
	}

	// Copy data from file to file
	// TODO: speed up by reading and writing in chunks
	fseek(file, 0L, SEEK_SET);
	bool result=true;
	for(uint16_t offset=0; 1; ++offset) {
		int value=fgetc(file);
		if (value==-1 || value>255)
			break;
		uint8_t value8=value;
		if (miniFsFileWrite(fs, destPath, offset, &value8, 1)!=1) {
			if (verbose)
				printf("warning unable to write complete data for '%s' representing '%s' (managed %i/%i bytes)\n", destPath, srcPath, offset, fileSize);
			result=false;
			break;
		}
	}

	fclose(file);

	return result;
}

bool miniFsExtraAddDir(MiniFs *fs, const char *dirPath, bool verbose) {
	// Loop over files in given dir and write each one to minifs
	DIR *dir=opendir(dirPath);
	if (dir==NULL) {
		if (verbose)
			printf("could not open directory '%s'\n", dirPath);
		return false;
	}

	struct dirent *dp;
	while((dp=readdir(dir))!=NULL) {
		if (strcmp(dp->d_name, ".gitignore")==0)
			continue;

		char fullName[100];
		sprintf(fullName , "%s/%s", dirPath, dp->d_name);

		struct stat stbuf;
		if (stat(fullName, &stbuf)==-1) {
			if (verbose)
				printf("warning unable to stat file '%s'\n", fullName);
			continue;
		}

		if ((stbuf.st_mode & S_IFMT)==S_IFDIR)
			continue; // Skip directories
		else
			if (!miniFsExtraAddFile(fs, dp->d_name, fullName, verbose)) {
				if (verbose)
					printf("warning unable to add file '%s'\n", fullName);
				return false;
			}
	}

	return true;
}
