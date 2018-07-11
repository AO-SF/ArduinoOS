#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernelfs.h"

void kernelBoot(void);
void kernelShutdown(void);

int main(int argc, char **argv) {
	// For now simply test file IO

	kernelBoot();

	#define DATALEN 16
	uint8_t data[DATALEN];

	KernelFsFd serialFd=kernelFsFileOpen("/dev/ttyUSB", KernelFsFileOpenFlagsRW);
	KernelFsFd randFd=kernelFsFileOpen("/dev/urandom", KernelFsFileOpenFlagsRO);

	KernelFsFileOffset readCount=kernelFsFileRead(randFd, data, DATALEN);
	for(int i=0; i<readCount; ++i) {
		char str[32];
		sprintf(str, "%i\n", data[i]);
		kernelFsFileWrite(serialFd, (uint8_t *)str, strlen(str)+1);
	}

	kernelFsFileClose(randFd);
	kernelFsFileClose(serialFd);

	kernelShutdown();

	return 0;
}

void kernelBoot(void) {
	// Init file system
	kernelFsInit();
}

void kernelShutdown(void) {
	// Quit file system
	kernelFsQuit();

	// Halt
#ifdef ARDUINO
	while(1)
		;
#else
	exit(0);
#endif
}
