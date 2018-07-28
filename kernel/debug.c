#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "debug.h"
#include "kernelfs.h"

void debugFsHelper(const char *path, int indent);

void debugFileIo(void) {
	#define DATALEN 16
	uint8_t data[DATALEN];

	// Open serial and random number generator
	KernelFsFd serialFd=kernelFsFileOpen("/dev/ttyS0");
	KernelFsFd randFd=kernelFsFileOpen("/dev/urandom");

	// Write bytes from random generator out to serial/stdout
	KernelFsFileOffset readCount=kernelFsFileRead(randFd, data, DATALEN);
	for(int i=0; i<readCount; ++i) {
		char str[32];
		sprintf(str, "%i\n", data[i]);
		kernelFsFileWrite(serialFd, (uint8_t *)str, strlen(str)+1);
	}

	// Close files
	kernelFsFileClose(randFd);
	kernelFsFileClose(serialFd);
}

void debugFs(void) {
	debugFsHelper("/", 0);
}

void debugFsHelper(const char *path, int indent) {
	// handle indent
	for(int i=0; i<indent; ++i)
		printf("  ");

	// file does not exist? (should not happen)
	if (!kernelFsFileExists(path))
		return;

	// print name
	char pathCopy[KernelFsPathMax];
	strcpy(pathCopy, path);
	char *dirname, *basename;
	if (strcmp(path, "/")==0) {
		dirname="";
		basename="/";
	} else
		kernelFsPathSplit(pathCopy, &dirname, &basename);

	printf("%s ", basename);

	// is file already open?
	if (kernelFsFileIsOpen(path)) {
		printf("(open)\n");
		return;
	}

	// open file/dir
	KernelFsFd fd=kernelFsFileOpen(path);
	if (fd==KernelFsFdInvalid) {
		printf("(error openning)\n");
		return;
	}

	bool isDir=kernelFsFileIsDir(path);

	// print first few bytes of content
	if (!isDir) {
#define CONTENTSIZE 8
		if (strcmp(path, "/dev/ttyS0")!=0) {
			for(int i=0; i<16-strlen(basename); ++i)
				printf(" ");
			uint8_t content[CONTENTSIZE];
			KernelFsFileOffset readCount=kernelFsFileRead(fd, content, CONTENTSIZE);
			printf("data:");
			for(KernelFsFileOffset i=0; i<readCount; ++i)
				printf(" 0x%02X", content[i]);
			if (readCount==CONTENTSIZE)
				printf(" ...");
		} else
			printf("(skipped)");
	}
#undef CONTENTSIZE

	printf("\n");

	// If directory print children recursively
	if (isDir) {
		char childPath[KernelFsPathMax];
		unsigned childNum;
		for(childNum=0; ; ++childNum) {
			if (!kernelFsDirectoryGetChild(fd, childNum, childPath))
				break;
			debugFsHelper(childPath, indent+1);
		}
	}

	// close file
	kernelFsFileClose(fd);
}

bool debugMiniFsAddFile(MiniFs *fs, const char *destPath, const char *srcPath) {
	// Open src file for reading
	FILE *file=fopen(srcPath, "r");
	if (file==NULL) {
		printf("warning unable to open file '%s' for reading\n", srcPath); // .....
		return false;
	}

	// Create file in minifs volume to represent this onernel.c:
	fseek(file, 0L, SEEK_END);
	uint16_t fileSize=ftell(file);
	if (!miniFsFileCreate(fs, destPath, fileSize)) {
		printf("warning unable to create file '%s' representing '%s'\n", destPath, srcPath); // .....
		fclose(file);
		return false;
	}

	// Copy data from file to file
	fseek(file, 0L, SEEK_SET);
	for(uint16_t offset=0; 1; ++offset) {
		int value=fgetc(file);
		if (value==-1)
			break;
		if (!miniFsFileWrite(fs, destPath, offset, value)) {
			printf("warning unable to write complete data for '%s' representing '%s' (managed %i bytes)\n", destPath, srcPath, offset);
			break;
		}
	}

	fclose(file);

	return true;
}

bool debugMiniFsAddDir(MiniFs *fs, const char *dirPath) {
	// Loop over files in given dir and write each one to minifs
	DIR *dir=opendir(dirPath);
	if (dir==NULL) {
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
			printf("warning unable to stat file '%s'\n", fullName);
			continue;
		}

		if ((stbuf.st_mode & S_IFMT)==S_IFDIR)
			continue; // Skip directories
		else
			debugMiniFsAddFile(fs, dp->d_name, fullName); // TODO: Check return
	}

	return true;
}

void debugLog(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	debugLogV(format, ap);
	va_end(ap);
}

void debugLogV(const char *format, va_list ap) {
	// TODO: Think about Arduino case
#ifndef ARDUINO
	vprintf(format, ap);
#endif
}
