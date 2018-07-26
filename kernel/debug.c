#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
