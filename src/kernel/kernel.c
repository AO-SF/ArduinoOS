#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ARDUINO
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif
#include <unistd.h>

#include "kernel.h"
#include "kernelfs.h"
#include "log.h"
#include "minifs.h"
#include "procman.h"
#include "progmembin.h"
#include "progmemlibcurses.h"
#include "progmemlibpin.h"
#include "progmemlibstdio.h"
#include "progmemlibstdmath.h"
#include "progmemlibstdproc.h"
#include "progmemlibstdmem.h"
#include "progmemlibstdstr.h"
#include "progmemlibstdtime.h"
#include "progmemusrbin.h"
#include "progmemman1.h"
#include "progmemman2.h"
#include "progmemman3.h"
#include "wrapper.h"

#define KernelPinNumMax 20

#define KernelTmpDataPoolSize (8*1024) // 8kb - used as ram (will have to be smaller on Uno presumably)
uint8_t *kernelTmpDataPool=NULL;

#define KernelBinSize PROGMEMbinDATASIZE
#define KernelLibCursesSize PROGMEMlibcursesDATASIZE
#define KernelLibPinSize PROGMEMlibpinDATASIZE
#define KernelLibStdIoSize PROGMEMlibstdioDATASIZE
#define KernelLibStdMathSize PROGMEMlibstdmathDATASIZE
#define KernelLibStdProcSize PROGMEMlibstdprocDATASIZE
#define KernelLibStdMemSize PROGMEMlibstdmemDATASIZE
#define KernelLibStdStrSize PROGMEMlibstdstrDATASIZE
#define KernelLibStdTimeSize PROGMEMlibstdtimeDATASIZE
#define KernelUsrBinSize PROGMEMusrbinDATASIZE
#define KernelMan1Size PROGMEMman1DATASIZE
#define KernelMan2Size PROGMEMman2DATASIZE
#define KernelMan3Size PROGMEMman3DATASIZE

#define KernelEepromTotalSize (8*1024) // Mega has 4kb for example
#define KernelEepromEtcOffset (0)
#define KernelEepromEtcSize (1*1024)
#define KernelEepromHomeOffset KernelEepromEtcSize
#define KernelEepromHomeSize (KernelEepromTotalSize-KernelEepromHomeOffset)
#ifndef ARDUINO
const char *kernelFakeEepromPath="./eeprom";
FILE *kernelFakeEepromFile=NULL;

int kernelTtyS0BytesAvailable=0; // We have to store this to avoid polling too often causing us to think no data is waiting

bool pinStates[KernelPinNumMax];

#endif

#ifndef ARDUINO
ProcManPid kernelReaderPid=ProcManPidMax;
#endif

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void kernelBoot(void);
void kernelShutdown(void);

void kernelHalt(void);

void kernelFatalError(const char *format, ...);
void kernelFatalErrorV(const char *format, va_list ap);

bool kernelRootGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelDevGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelLibGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelLibStdGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelMediaGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelUsrGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelUsrManGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
int kernelBinReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibCursesReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibPinReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibStdIoReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibStdMathReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibStdProcReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibStdMemReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibStdStrReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelLibStdTimeReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelMan1ReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelMan2ReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelMan3ReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelHomeReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelHomeWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);
uint8_t kernelHomeMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelHomeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelEtcReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelEtcWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);
uint8_t kernelEtcMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelEtcMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelTmpReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);
uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelDevZeroReadFunctor(void *userData);
bool kernelDevZeroCanReadFunctor(void *userData);
bool kernelDevZeroWriteFunctor(uint8_t value, void *userData);
int kernelDevFullReadFunctor(void *userData);
bool kernelDevFullCanReadFunctor(void *userData);
bool kernelDevFullWriteFunctor(uint8_t value, void *userData);
int kernelDevNullReadFunctor(void *userData);
bool kernelDevNullCanReadFunctor(void *userData);
bool kernelDevNullWriteFunctor(uint8_t value, void *userData);
int kernelDevURandomReadFunctor(void *userData);
bool kernelDevURandomCanReadFunctor(void *userData);
bool kernelDevURandomWriteFunctor(uint8_t value, void *userData);
int kernelDevTtyS0ReadFunctor(void *userData);
bool kernelDevTtyS0CanReadFunctor(void *userData);
bool kernelDevTtyS0WriteFunctor(uint8_t value, void *userData);
int kernelUsrBinReadFunctor(KernelFsFileOffset addr, void *userData);
int kernelDevPinReadFunctor(void *userData);
bool kernelDevPinCanReadFunctor(void *userData);
bool kernelDevPinWriteFunctor(uint8_t value, void *userData);

#ifndef ARDUINO
void kernelSigIntHandler(int sig);
#endif

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
	// setup and main loop
	setup();
	loop();

	return 0;
}

#endif

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void kernelBoot(void) {
	kernelLog(LogTypeInfo, "booting\n");

#ifndef ARDUINO
	// ON the Arduino we can leave this at 0 but otherwise we have to save offset
	kernelBootTime=millisRaw();
	kernelLog(LogTypeInfo, "set kernel boot time to %lu (PC wrapper)\n", kernelBootTime);
#endif

	// PC only - set pinStates array to all off
#ifndef ARDUINO
	memset(pinStates, 0, sizeof(pinStates));
#endif

	// PC only - register sigint handler so we can pass this signal onto e.g. the shell
#ifndef ARDUINO
    signal(SIGINT, kernelSigIntHandler); // TODO: Check return.
#endif

	// Arduino-only: connect to serial (ready to mount as /dev/ttyS0).
#ifdef ARDUINO
	Serial.begin(9600);
	while (!Serial) ;
	kernelLog(LogTypeInfo, "initialised serial\n");
#endif

	// Allocate space for /tmp
	kernelTmpDataPool=malloc(KernelTmpDataPoolSize);
	if (kernelTmpDataPool==NULL)
		kernelFatalError("could not allocate /tmp data pool (size %u)\n", KernelTmpDataPoolSize);
	kernelLog(LogTypeInfo, "allocated /tmp space (size %u)\n", KernelTmpDataPoolSize);

	// Non-arduino-only: create pretend EEPROM storage in a local file
#ifndef ARDUINO
	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "a+");
	if (kernelFakeEepromFile==NULL)
		kernelFatalError("could not open/create pseudo EEPROM storage file at '%s' (PC wrapper)\n", kernelFakeEepromPath);
	fclose(kernelFakeEepromFile);

	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "r+");
	if (kernelFakeEepromFile==NULL)
		kernelFatalError("could not open pseudo EEPROM storage file at '%s' for reading and writing (PC wrapper)\n", kernelFakeEepromPath);
	fseek(kernelFakeEepromFile, 0L, SEEK_END);
	int eepromInitialSize=ftell(kernelFakeEepromFile);

	while(eepromInitialSize<KernelEepromTotalSize) {
		fputc(0xFF, kernelFakeEepromFile);
		++eepromInitialSize;
	}

	kernelLog(LogTypeInfo, "openned pseudo EEPROM storage file (PC wrapper)\n");
#endif

	// Format /home if it does not look like it has been already
	MiniFs homeMiniFs;
	if (miniFsMountSafe(&homeMiniFs, &kernelHomeMiniFsReadFunctor, &kernelHomeMiniFsWriteFunctor, NULL)) {
		miniFsUnmount(&homeMiniFs); // Unmount so we can mount again when we initialise the file system
		kernelLog(LogTypeInfo, "/home volume already exists\n");
	} else {
		if (!miniFsFormat(&kernelHomeMiniFsWriteFunctor, NULL, KernelEepromHomeSize))
			kernelFatalError("could not format new /home volume\n");
		kernelLog(LogTypeInfo, "formatted new /home volume (size %u)\n", KernelEepromHomeSize);
	}

	// Format RAM used for /tmp
	if (!miniFsFormat(&kernelTmpMiniFsWriteFunctor, NULL, KernelTmpDataPoolSize))
		kernelFatalError("could not format /tmp volume\n");
	kernelLog(LogTypeInfo, "formatted volume representing /tmp\n");

	// Init file system and add virtual devices
	kernelFsInit();

	bool error=false;
	error|=!kernelFsAddDirectoryDeviceFile("/", &kernelRootGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/bin", KernelFsBlockDeviceFormatCustomMiniFs, &kernelBinReadFunctor, NULL, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/dev", &kernelDevGetChildFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/zero", &kernelDevZeroReadFunctor, &kernelDevZeroCanReadFunctor, &kernelDevZeroWriteFunctor, NULL);
	error|=!kernelFsAddCharacterDeviceFile("/dev/full", &kernelDevFullReadFunctor, &kernelDevFullCanReadFunctor, &kernelDevFullWriteFunctor, NULL);
	error|=!kernelFsAddCharacterDeviceFile("/dev/null", &kernelDevNullReadFunctor, &kernelDevNullCanReadFunctor, &kernelDevNullWriteFunctor, NULL);
	error|=!kernelFsAddCharacterDeviceFile("/dev/urandom", &kernelDevURandomReadFunctor, &kernelDevURandomCanReadFunctor, &kernelDevURandomWriteFunctor, NULL);
	for(uint8_t pinNum=0; pinNum<KernelPinNumMax; ++pinNum) {
		char pinDevicePath[64];
		sprintf(pinDevicePath, "/dev/pin%u", pinNum);
		error|=!kernelFsAddCharacterDeviceFile(pinDevicePath, &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, (void *)(uintptr_t)pinNum);
	}
	error|=!kernelFsAddCharacterDeviceFile("/dev/ttyS0", &kernelDevTtyS0ReadFunctor, &kernelDevTtyS0CanReadFunctor, &kernelDevTtyS0WriteFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/etc", KernelFsBlockDeviceFormatCustomMiniFs, &kernelEtcReadFunctor, kernelEtcWriteFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/home", KernelFsBlockDeviceFormatCustomMiniFs, &kernelHomeReadFunctor, kernelHomeWriteFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/lib", &kernelLibGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/lib/curses", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibCursesReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/pin", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibPinReadFunctor, NULL, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/lib/std", &kernelLibStdGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/io", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibStdIoReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/math", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibStdMathReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/proc", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibStdProcReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/mem", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibStdMemReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/str", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibStdStrReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/time", KernelFsBlockDeviceFormatCustomMiniFs, &kernelLibStdTimeReadFunctor, NULL, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/media", &kernelMediaGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/tmp", KernelFsBlockDeviceFormatCustomMiniFs, &kernelTmpReadFunctor, &kernelTmpWriteFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/usr", &kernelUsrGetChildFunctor);
	error|=!kernelFsAddDirectoryDeviceFile("/usr/man", &kernelUsrManGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/usr/man/1", KernelFsBlockDeviceFormatCustomMiniFs, &kernelMan1ReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/usr/man/2", KernelFsBlockDeviceFormatCustomMiniFs, &kernelMan2ReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/usr/man/3", KernelFsBlockDeviceFormatCustomMiniFs, &kernelMan3ReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile("/usr/bin", KernelFsBlockDeviceFormatCustomMiniFs, &kernelUsrBinReadFunctor, NULL, NULL);

	if (error)
		kernelFatalError("could not initialise filesystem\n");
	kernelLog(LogTypeInfo, "initialised filesystem\n");

	// Initialise process manager and start init process
	procManInit();
	kernelLog(LogTypeInfo, "initialised process manager\n");

	kernelLog(LogTypeInfo, "starting init\n");
	if (procManProcessNew("/bin/init")==ProcManPidMax)
		kernelFatalError("could not start init at '%s'\n", "/bin/init");
}

void kernelShutdown(void) {
	kernelLog(LogTypeInfo, "shutting down\n");

	// Quit process manager
	kernelLog(LogTypeInfo, "killing all processes\n");
	procManQuit();

	// Quit file system
	kernelLog(LogTypeInfo, "unmounting filesystem\n");
	kernelFsQuit();

	// Arduino-only: close serial connection (was mounted as /dev/ttyS0).
#ifdef ARDUINO
	kernelLog(LogTypeInfo, "closing serial connection\n");
	Serial.end();
#endif

	// Free /tmp memory pool
	kernelLog(LogTypeInfo, "freeing /tmp space\n");
	free(kernelTmpDataPool);

	// Non-arduino-only: close pretend EEPROM storage file
#ifndef ARDUINO
	kernelLog(LogTypeInfo, "closing pseudo EEPROM storage file (PC wrapper)\n");
	fclose(kernelFakeEepromFile); // TODO: Check return
#endif

	// Halt
	kernelHalt();
}

void kernelHalt(void) {
#ifdef ARDUINO
	kernelLog(LogTypeInfo, "halting\n");
	while(1)
		;
#else
	kernelLog(LogTypeInfo, "exiting (PC wrapper)\n");
	exit(0);
#endif
}

void kernelFatalError(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelFatalErrorV(format, ap);
	va_end(ap);
}

void kernelFatalErrorV(const char *format, va_list ap) {
	kernelLogV(LogTypeError, format, ap);
	kernelHalt();
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
		case 7: strcpy(childPath, "/etc"); return true; break;
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
	if (childNum>=5 && childNum<5+KernelPinNumMax) {
		sprintf(childPath, "/dev/pin%u", childNum-5);
		return true;
	}
	return false;
}

bool kernelLibGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/lib/std"); return true; break;
		case 1: strcpy(childPath, "/lib/curses"); return true; break;
		case 2: strcpy(childPath, "/lib/pin"); return true; break;
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
		case 1: strcpy(childPath, "/usr/man"); return true; break;
	}
	return false;
}

bool kernelUsrManGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/usr/man/1"); return true; break;
		case 1: strcpy(childPath, "/usr/man/2"); return true; break;
		case 2: strcpy(childPath, "/usr/man/3"); return true; break;
	}
	return false;
}

int kernelBinReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelBinSize);
	return progmembinData[addr];
}

int kernelLibCursesReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibCursesSize);
	return progmemlibcursesData[addr];
}

int kernelLibPinReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibPinSize);
	return progmemlibpinData[addr];
}

int kernelLibStdIoReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdIoSize);
	return progmemlibstdioData[addr];
}

int kernelLibStdMathReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdMathSize);
	return progmemlibstdmathData[addr];
}

int kernelLibStdProcReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdProcSize);
	return progmemlibstdprocData[addr];
}

int kernelLibStdMemReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdProcSize);
	return progmemlibstdmemData[addr];
}

int kernelLibStdStrReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdStrSize);
	return progmemlibstdstrData[addr];
}

int kernelLibStdTimeReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdTimeSize);
	return progmemlibstdtimeData[addr];
}

int kernelMan1ReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelMan1Size);
	return progmemman1Data[addr];
}

int kernelMan2ReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelMan2Size);
	return progmemman2Data[addr];
}

int kernelMan3ReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelMan3Size);
	return progmemman3Data[addr];
}

int kernelHomeReadFunctor(KernelFsFileOffset addr, void *userData) {
	addr+=KernelEepromHomeOffset;
#ifdef ARDUINO
	return EEPROM.read(addr);
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, "could not seek to addr %u in home read functor\n", addr);
		return -1;
	}
	int c=fgetc(kernelFakeEepromFile);
	if (c==EOF) {
		kernelLog(LogTypeWarning, "could not read at addr %u in home read functor\n", addr);
		return -1;
	}
	return c;
#endif
}

bool kernelHomeWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	addr+=KernelEepromHomeOffset;
#ifdef ARDUINO
	EEPROM.update(addr, value);
	return true;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, "could not seek to addr %u in home write functor\n", addr);
		return false;
	}
	if (fputc(value, kernelFakeEepromFile)==EOF) {
		kernelLog(LogTypeWarning, "could not write to addr %u in home write functor\n", addr);
		return false;
	}

	return true;
#endif
}

uint8_t kernelHomeMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelHomeReadFunctor(addr, userData);
	assert(value>=0 && value<256);
	return value;
}

void kernelHomeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelHomeWriteFunctor(addr, value, userData);
	assert(result);
}

int kernelEtcReadFunctor(KernelFsFileOffset addr, void *userData) {
	addr+=KernelEepromEtcOffset;
#ifdef ARDUINO
	return EEPROM.read(addr);
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, "could not seek to addr %u in etc read functor\n", addr);
		return -1;
	}
	int c=fgetc(kernelFakeEepromFile);
	if (c==EOF) {
		kernelLog(LogTypeWarning, "could not read at addr %u in etc read functor\n", addr);
		return -1;
	}
	return c;
#endif
}

bool kernelEtcWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	addr+=KernelEepromEtcOffset;
#ifdef ARDUINO
	EEPROM.update(addr, value);
	return true;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, "could not seek to addr %u in etc write functor\n", addr);
		return false;
	}
	if (fputc(value, kernelFakeEepromFile)==EOF) {
		kernelLog(LogTypeWarning, "could not write to addr %u in etc write functor\n", addr);
		return false;
	}

	return true;
#endif
}

uint8_t kernelEtcMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelEtcReadFunctor(addr, userData);
	assert(value>=0 && value<256);
	return value;
}

void kernelEtcMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelEtcWriteFunctor(addr, value, userData);
	assert(result);
}

int kernelTmpReadFunctor(KernelFsFileOffset addr, void *userData) {
	return kernelTmpDataPool[addr];
}

bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	kernelTmpDataPool[addr]=value;
	return true;
}

uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelTmpReadFunctor(addr, userData);
	assert(value>=0 && value<256);
	return value;
}

void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelTmpWriteFunctor(addr, value, userData);
	assert(result);
}

int kernelDevZeroReadFunctor(void *userData) {
	return 0;
}

bool kernelDevZeroCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevZeroWriteFunctor(uint8_t value, void *userData) {
	return true;
}

int kernelDevFullReadFunctor(void *userData) {
	return 0;
}

bool kernelDevFullCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevFullWriteFunctor(uint8_t value, void *userData) {
	return false;
}

int kernelDevNullReadFunctor(void *userData) {
	return 0;
}

bool kernelDevNullCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevNullWriteFunctor(uint8_t value, void *userData) {
	return true;
}

int kernelDevURandomReadFunctor(void *userData) {
	return rand()&0xFF;
}

bool kernelDevURandomCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevURandomWriteFunctor(uint8_t value, void *userData) {
	return false;
}

int kernelDevTtyS0ReadFunctor(void *userData) {
#ifdef ARDUINO
	return Serial.read();
#else
	if (kernelTtyS0BytesAvailable==0)
		kernelLog(LogTypeWarning, "kernelTtyS0BytesAvailable=0 going into kernelDevTtyS0ReadFunctor\n");
	int c=getchar();
	if (c==EOF)
		return -1;
	if (kernelTtyS0BytesAvailable>0)
		--kernelTtyS0BytesAvailable;
	return c;
#endif
}

bool kernelDevTtyS0CanReadFunctor(void *userData) {
#ifdef ARDUINO
	return (Serial.available()>0);
#else
	// If we still think there are bytes waiting to be read, return true immediately
	if (kernelTtyS0BytesAvailable>0)
		return true;

	// Otherwise poll for input events on stdin
	struct pollfd pollFds[0];
	memset(pollFds, 0, sizeof(pollFds));
	pollFds[0].fd=STDIN_FILENO;
	pollFds[0].events=POLLIN;
	if (poll(pollFds, 1, 0)<=0)
		return false;

	if (!(pollFds[0].revents & POLLIN))
		return false;

	// Call ioctl to find number of bytes available
	ioctl(STDIN_FILENO, FIONREAD, &kernelTtyS0BytesAvailable);
	return true;
#endif
}

bool kernelDevTtyS0WriteFunctor(uint8_t value, void *userData) {
#ifdef ARDUINO
	return (Serial.write(value)==1);
#else
	if (putchar(value)!=value)
		return false;
	fflush(stdout);
	return true;
#endif
}

int kernelUsrBinReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelUsrBinSize);
	return progmemusrbinData[addr];
}

int kernelDevPinReadFunctor(void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;
	if (pinNum>=KernelPinNumMax) {
		kernelLog(LogTypeWarning, "bad pin %u in read\n", pinNum);
		return -1;
	}
#ifdef ARDUINO
	// TODO: this pinRead() essentially
	kernelLog(LogTypeWarning, "pin %u read - not implemented\n", pinNum);
	return -1
#else
	kernelLog(LogTypeInfo, "pin %u read - value %u\n", pinNum, pinStates[pinNum]);
	return pinStates[pinNum];
#endif
}

bool kernelDevPinCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevPinWriteFunctor(uint8_t value, void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;
	if (pinNum>=KernelPinNumMax) {
		kernelLog(LogTypeWarning, "bad pin %u in write\n", pinNum);
		return false;
	}
#ifdef ARDUINO
	// TODO: this pinWrite() essentially
		kernelLog(LogTypeWarning, "pin %u write - not implemented\n", pinNum);
	return false
#else
	kernelLog(LogTypeInfo, "pin %u write - new value %u\n", pinNum, value);
	pinStates[pinNum]=(value!=0);
	return true;
#endif
}

#ifndef ARDUINO
void kernelSigIntHandler(int sig) {
	procManProcessSendSignal(kernelReaderPid, ByteCodeSignalIdInterrupt);
}
#endif
