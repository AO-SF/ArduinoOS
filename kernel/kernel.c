#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ARDUINO
#include <termios.h>
#endif
#include <unistd.h>

#include "debug.h"
#include "kernel.h"
#include "kernelfs.h"
#include "minifs.h"
#include "procman.h"
#include "progmembin.h"
#include "progmemlibcurses.h"
#include "progmemlibstdio.h"
#include "progmemlibstdmath.h"
#include "progmemlibstdproc.h"
#include "progmemlibstdmem.h"
#include "progmemlibstdstr.h"
#include "progmemlibstdtime.h"
#include "progmemusrbin.h"
#include "wrapper.h"

#define KernelTmpDataPoolSize (8*1024) // 8kb - used as ram (will have to be smaller on Uno presumably)
uint8_t *kernelTmpDataPool=NULL;

#define KernelBinSize PROGMEMbinDATASIZE
#define KernelLibCursesSize PROGMEMlibcursesDATASIZE
#define KernelLibStdIoSize PROGMEMlibstdioDATASIZE
#define KernelLibStdMathSize PROGMEMlibstdmathDATASIZE
#define KernelLibStdProcSize PROGMEMlibstdprocDATASIZE
#define KernelLibStdMemSize PROGMEMlibstdmemDATASIZE
#define KernelLibStdStrSize PROGMEMlibstdstrDATASIZE
#define KernelLibStdTimeSize PROGMEMlibstdtimeDATASIZE
#define KernelUsrBinSize PROGMEMusrbinDATASIZE

#define KernelEepromSize (8*1024) // Mega has 4kb for example
#ifndef ARDUINO
const char *kernelFakeEepromPath="./eeprom";
FILE *kernelFakeEepromFile=NULL;
#endif

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void kernelBoot(void);
void kernelShutdown(void);

bool kernelRootGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelDevGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelLibGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelLibStdGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelMediaGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelUsrGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
int kernelBinReadFunctor(KernelFsFileOffset addr);
int kernelLibCursesReadFunctor(KernelFsFileOffset addr);
int kernelLibStdIoReadFunctor(KernelFsFileOffset addr);
int kernelLibStdMathReadFunctor(KernelFsFileOffset addr);
int kernelLibStdProcReadFunctor(KernelFsFileOffset addr);
int kernelLibStdMemReadFunctor(KernelFsFileOffset addr);
int kernelLibStdStrReadFunctor(KernelFsFileOffset addr);
int kernelLibStdTimeReadFunctor(KernelFsFileOffset addr);
int kernelHomeReadFunctor(KernelFsFileOffset addr);
bool kernelHomeWriteFunctor(KernelFsFileOffset addr, uint8_t value);
uint8_t kernelHomeMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelHomeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelTmpReadFunctor(KernelFsFileOffset addr);
bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value);
uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelDevZeroReadFunctor(bool block);
bool kernelDevZeroWriteFunctor(uint8_t value);
int kernelDevFullReadFunctor(bool block);
bool kernelDevFullWriteFunctor(uint8_t value);
int kernelDevNullReadFunctor(bool block);
bool kernelDevNullWriteFunctor(uint8_t value);
int kernelDevURandomReadFunctor(bool block);
bool kernelDevURandomWriteFunctor(uint8_t value);
int kernelDevTtyS0ReadFunctor(bool block);
bool kernelDevTtyS0WriteFunctor(uint8_t value);
int kernelUsrBinReadFunctor(KernelFsFileOffset addr);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void setup() {
	// Init
	kernelBoot();

	// Run processes
	while(procManGetProcessCount()>0)
		procManTickAll();

	// Quit
	kernelShutdown();
}

void loop() {
	// Do nothing - we should never reach here
}

#ifndef ARDUINO

int main(int argc, char **argv) {
	// save terminal settings
	static struct termios termOld, termNew;
	tcgetattr(STDIN_FILENO, &termOld);

	// change terminal settings to avoid waiting for a newline before submitting any data
	termNew=termOld;
	termNew.c_lflag&=~ICANON;
	tcsetattr(STDIN_FILENO, TCSANOW, &termNew);

	// setup and main loop
	setup();
	loop();

	// restore terminal settings
	tcsetattr(STDIN_FILENO, TCSANOW, &termOld);

	return 0;
}

#endif

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void kernelBoot(void) {
#ifndef ARDUINO
	// ON the Arduino we can leave this at 0 but otherwise we have to save offset
	kernelBootTime=millisRaw();
#endif

	// Arduino-only: connect to serial (ready to mount as /dev/ttyS0).
#ifdef ARDUINO
	Serial.begin(9600);
	while (!Serial) ;
#endif

	// Allocate space for /tmp
	kernelTmpDataPool=malloc(KernelTmpDataPoolSize); // TODO: Check return

	// Non-arduino-only: create pretend EEPROM storage in a local file
#ifndef ARDUINO
	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "a+"); // TODO: Check return
	fclose(kernelFakeEepromFile);

	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "r+"); // TODO: Check return

	fseek(kernelFakeEepromFile, 0L, SEEK_END);
	int eepromInitialSize=ftell(kernelFakeEepromFile);

	while(eepromInitialSize<KernelEepromSize) {
		fputc(0xFF, kernelFakeEepromFile);
		++eepromInitialSize;
	}
#endif

	// Format /home if it does not look like it has been already
	MiniFs homeMiniFs;
	if (miniFsMountSafe(&homeMiniFs, &kernelHomeMiniFsReadFunctor, &kernelHomeMiniFsWriteFunctor, NULL))
		miniFsUnmount(&homeMiniFs); // Unmount so we can mount again when we initialise the file system
	else {
		miniFsFormat(&kernelHomeMiniFsWriteFunctor, NULL, KernelEepromSize); // TODO: check return

		// Add a few example files
		// TODO: this is only temporary
		miniFsMountSafe(&homeMiniFs, &kernelHomeMiniFsReadFunctor, &kernelHomeMiniFsWriteFunctor, NULL);
		debugMiniFsAddDir(&homeMiniFs, "../homemockup");
		miniFsDebug(&homeMiniFs);
		miniFsUnmount(&homeMiniFs);
	}

	// Format RAM used for /tmp
	miniFsFormat(&kernelTmpMiniFsWriteFunctor, NULL, KernelTmpDataPoolSize); // TODO: check return

	// Init file system and add virtual devices
	kernelFsInit();

	bool error=false;
	error|=!kernelFsAddDirectoryDeviceFile("/", &kernelRootGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/bin", KernelFsBlockDeviceFormatCustomMiniFs, KernelBinSize, &kernelBinReadFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/dev", &kernelDevGetChildFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/zero", &kernelDevZeroReadFunctor, &kernelDevZeroWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/full", &kernelDevFullReadFunctor, &kernelDevFullWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/null", &kernelDevNullReadFunctor, &kernelDevNullWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/urandom", &kernelDevURandomReadFunctor, &kernelDevURandomWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/ttyS0", &kernelDevTtyS0ReadFunctor, &kernelDevTtyS0WriteFunctor);
	error|=!kernelFsAddBlockDeviceFile("/home", KernelFsBlockDeviceFormatCustomMiniFs, KernelEepromSize, &kernelHomeReadFunctor, kernelHomeWriteFunctor);
	error|=!kernelFsAddDirectoryDeviceFile("/lib", &kernelLibGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/lib/curses", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibCursesSize, &kernelLibCursesReadFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/lib/std", &kernelLibStdGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/io", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdIoSize, &kernelLibStdIoReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/math", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdMathSize, &kernelLibStdMathReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/proc", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdProcSize, &kernelLibStdProcReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/mem", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdMemSize, &kernelLibStdMemReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/str", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdStrSize, &kernelLibStdStrReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/time", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdTimeSize, &kernelLibStdTimeReadFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/media", &kernelMediaGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/tmp", KernelFsBlockDeviceFormatCustomMiniFs, KernelTmpDataPoolSize, &kernelTmpReadFunctor, kernelTmpWriteFunctor);
	error|=!kernelFsAddDirectoryDeviceFile("/usr", &kernelUsrGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/usr/bin", KernelFsBlockDeviceFormatCustomMiniFs, KernelUsrBinSize, &kernelUsrBinReadFunctor, NULL);
	// TODO: handle error

	// Initialise process manager and start init process
	procManInit();

	procManProcessNew("/bin/init"); // TODO: Check return
}

void kernelShutdown(void) {
	// Quit process manager
	procManQuit();

	// Quit file system
	kernelFsQuit();

	// Arduino-only: close serial connection (was mounted as /dev/ttyS0).
#ifdef ARDUINO
	Serial.end();
#endif

	// Free /tmp memory pool
	free(kernelTmpDataPool);

	// Non-arduino-only: close pretend EEPROM storage file
#ifndef ARDUINO
	fclose(kernelFakeEepromFile); // TODO: Check return
#endif

	// Halt
#ifdef ARDUINO
	while(1)
		;
#else
	exit(0);
#endif
}

bool kernelRootGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/bin"); return true; break;
		case 1: strcpy(childPath, "/dev"); return true; break;
		case 2: strcpy(childPath, "/lib"); return true; break;
		case 3: strcpy(childPath, "/home"); return true; break;
		case 4: strcpy(childPath, "/media"); return true; break;
		case 5: strcpy(childPath, "/tmp"); return true; break;
		case 6: strcpy(childPath, "/usr"); return true; break;
	}
	return false;
}

bool kernelDevGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/dev/zero"); return true; break;
		case 1: strcpy(childPath, "/dev/full"); return true; break;
		case 2: strcpy(childPath, "/dev/null"); return true; break;
		case 3: strcpy(childPath, "/dev/urandom"); return true; break;
		case 4: strcpy(childPath, "/dev/ttyS0"); return true; break;
	}
	return false;
}

bool kernelLibGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/lib/std"); return true; break;
		case 1: strcpy(childPath, "/lib/curses"); return true; break;
	}
	return false;
}

bool kernelLibStdGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/lib/std/io"); return true; break;
		case 1: strcpy(childPath, "/lib/std/math"); return true; break;
		case 2: strcpy(childPath, "/lib/std/proc"); return true; break;
		case 3: strcpy(childPath, "/lib/std/mem"); return true; break;
		case 4: strcpy(childPath, "/lib/std/str"); return true; break;
		case 5: strcpy(childPath, "/lib/std/time"); return true; break;
	}
	return false;
}

bool kernelMediaGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	// TODO: this (along with implementing mounting of external drives)
	return false;
}

bool kernelUsrGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/usr/bin"); return true; break;
	}
	return false;
}

int kernelBinReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelBinSize);
	return progmembinData[addr];
}

int kernelLibCursesReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibCursesSize);
	return progmemlibcursesData[addr];
}

int kernelLibStdIoReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdIoSize);
	return progmemlibstdioData[addr];
}

int kernelLibStdMathReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdMathSize);
	return progmemlibstdmathData[addr];
}

int kernelLibStdProcReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdProcSize);
	return progmemlibstdprocData[addr];
}

int kernelLibStdMemReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdProcSize);
	return progmemlibstdmemData[addr];
}

int kernelLibStdStrReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdStrSize);
	return progmemlibstdstrData[addr];
}

int kernelLibStdTimeReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdTimeSize);
	return progmemlibstdtimeData[addr];
}

int kernelHomeReadFunctor(KernelFsFileOffset addr) {
#ifdef ARDUINO
	return EEPROM.read(addr);
#else
	int res=fseek(kernelFakeEepromFile, addr, SEEK_SET);
	assert(res==0);
	assert(ftell(kernelFakeEepromFile)==addr);
	int c=fgetc(kernelFakeEepromFile);
	if (c==EOF)
		return -1;
	else
		return c;
#endif
}

bool kernelHomeWriteFunctor(KernelFsFileOffset addr, uint8_t value) {
#ifdef ARDUINO
	EEPROM.update(addr, value);
	return true;
#else
	int res=fseek(kernelFakeEepromFile, addr, SEEK_SET);
	assert(res==0);
	assert(ftell(kernelFakeEepromFile)==addr);
	return (fputc(value, kernelFakeEepromFile)!=EOF);
#endif
}

uint8_t kernelHomeMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelHomeReadFunctor(addr);
	assert(value>=0 && value<256);
	return value;
}

void kernelHomeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelHomeWriteFunctor(addr, value);
	assert(result);
}

int kernelTmpReadFunctor(KernelFsFileOffset addr) {
	return kernelTmpDataPool[addr];
}

bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value) {
	kernelTmpDataPool[addr]=value;
	return true;
}

uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelTmpReadFunctor(addr);
	assert(value>=0 && value<256);
	return value;
}

void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelTmpWriteFunctor(addr, value);
	assert(result);
}

int kernelDevZeroReadFunctor(bool block) {
	return 0;
}

bool kernelDevZeroWriteFunctor(uint8_t value) {
	return true;
}

int kernelDevFullReadFunctor(bool block) {
	return 0;
}

bool kernelDevFullWriteFunctor(uint8_t value) {
	return false;
}

int kernelDevNullReadFunctor(bool block) {
	return 0;
}

bool kernelDevNullWriteFunctor(uint8_t value) {
	return true;
}

int kernelDevURandomReadFunctor(bool block) {
	return rand()&0xFF;
}

bool kernelDevURandomWriteFunctor(uint8_t value) {
	return false;
}

int kernelDevTtyS0ReadFunctor(bool block) {
#ifdef ARDUINO
	if (!block && Serial.available()<1)
		return -1;
	return Serial.read();
#else
	if (!block) {
		struct pollfd pollFds[0];
		memset(pollFds, 0, sizeof(pollFds));
		pollFds[0].fd=STDIN_FILENO;
		pollFds[0].events=POLLIN;
		if (poll(pollFds, 1, 0)<=0)
			return -1;

		if (!(pollFds[0].revents & POLLIN))
			return -1;
	}

	int c=getchar();
	return (c!=EOF ? c : -1);
#endif
}

bool kernelDevTtyS0WriteFunctor(uint8_t value) {
#ifdef ARDUINO
	return (Serial.write(value)==1);
#else
	if (putchar(value)!=value)
		return false;
	fflush(stdout);
	return true;
#endif
}

int kernelUsrBinReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelUsrBinSize);
	return progmemusrbinData[addr];
}
