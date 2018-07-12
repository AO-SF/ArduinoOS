#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernelfs.h"

void kernelBoot(void);
void kernelShutdown(void);

int kernelDevZeroReadFunctor(void);
bool kernelDevZeroWriteFunctor(uint8_t value);
int kernelDevNullReadFunctor(void);
bool kernelDevNullWriteFunctor(uint8_t value);
int kernelDevURandomReadFunctor(void);
bool kernelDevURandomWriteFunctor(uint8_t value);
int kernelDevTtyS0ReadFunctor(void);
bool kernelDevTtyS0WriteFunctor(uint8_t value);

void kernelTestIo(void) {
	#define DATALEN 16
	uint8_t data[DATALEN];

	// Open serial and random number generator
	KernelFsFd serialFd=kernelFsFileOpen("/dev/ttyS0", KernelFsFileOpenFlagsRW);
	KernelFsFd randFd=kernelFsFileOpen("/dev/urandom", KernelFsFileOpenFlagsRO);

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

int main(int argc, char **argv) {
	// Init
	kernelBoot();

	// For now simply test file IO
	kernelTestIo();

	// Quit
	kernelShutdown();

	return 0;
}

void kernelBoot(void) {
	// Arduino-only: connect to serial (ready to mount as /dev/ttyS0).
#ifdef ARDUINO
	Serial.begin(9600);
	while (!Serial) ;
#endif

	// Init file system and add virtual devices
	kernelFsInit();

	bool error=false;

	error|=kernelFsAddCharacterDeviceFile("/dev/zero", &kernelDevZeroReadFunctor, &kernelDevZeroWriteFunctor);
	error|=kernelFsAddCharacterDeviceFile("/dev/null", &kernelDevNullReadFunctor, &kernelDevNullWriteFunctor);
	error|=kernelFsAddCharacterDeviceFile("/dev/urandom", &kernelDevURandomReadFunctor, &kernelDevURandomWriteFunctor);
	error|=kernelFsAddCharacterDeviceFile("/dev/ttyS0", &kernelDevTtyS0ReadFunctor, &kernelDevTtyS0WriteFunctor);

	// TODO: handle error
}

void kernelShutdown(void) {
	// Quit file system
	kernelFsQuit();

	// Arduino-only: close serial connection (was mounted as /dev/ttyS0).
#ifdef ARDUINO
	Serial.end();
#endif

	// Halt
#ifdef ARDUINO
	while(1)
		;
#else
	exit(0);
#endif
}

int kernelDevZeroReadFunctor(void) {
	return 0;
}

bool kernelDevZeroWriteFunctor(uint8_t value) {
	return false;
}

int kernelDevNullReadFunctor(void) {
	return 0;
}

bool kernelDevNullWriteFunctor(uint8_t value) {
	return true;
}

int kernelDevURandomReadFunctor(void) {
	return rand()&0xFF;
}

bool kernelDevURandomWriteFunctor(uint8_t value) {
	return false;
}

int kernelDevTtyS0ReadFunctor(void) {
#ifdef ARDUINO
	return Serial.read();
#else
	int c=getchar();
	return (c!=EOF ? c : -1);
#endif
}

bool kernelDevTtyS0WriteFunctor(uint8_t value) {
#ifdef ARDUINO
	return (Serial.write(value)==1);
#else
	return (putchar(value)==value);
#endif
}
