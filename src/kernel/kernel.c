#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#else
#include <unistd.h>
#endif

#include "hwdevice.h"
#include "kernel.h"
#include "kernelfs.h"
#include "ktime.h"
#include "log.h"
#include "minifs.h"
#include "pins.h"
#include "procman.h"
#include "spi.h"
#include "tty.h"
#include "util.h"

#include "commonprogmem.h"

#ifdef KERNELCUSTOMRAMSIZE
#define KernelTmpDataPoolSize KERNELCUSTOMRAMSIZE
#else
#define KernelTmpDataPoolSize (2*1024) // 2kb - used as ram (note: Arduino Mega only has 8kb total)
#endif
uint8_t kernelTmpDataPool[KernelTmpDataPoolSize];

#define KernelEepromTotalSize (4*1024) // Mega has 4kb for example
#define KernelEepromEtcOffset (0)
#define KernelEepromEtcSize (1*1024)
#define KernelEepromDevEepromOffset KernelEepromEtcSize
#define KernelEepromDevEepromSize (KernelEepromTotalSize-KernelEepromDevEepromOffset)

#ifndef ARDUINO
const char *kernelFakeEepromPath="./eeprom";
FILE *kernelFakeEepromFile=NULL;
bool kernelFlagProfile=false;
#endif

KernelFsFd kernelSpiLockFd=KernelFsFdInvalid;
uint8_t kernelSpiSlaveSelectPin;

KernelState kernelState=KernelStateInvalid;
KTime kernelStateTime=0;

#define kernelFatalError(format, ...) do { kernelLog(LogTypeError, format, ##__VA_ARGS__); kernelHalt(); } while(0)

typedef enum {
	KernelVirtualDevFileFull,
	KernelVirtualDevFileNull,
	KernelVirtualDevFileTtyS0,
	KernelVirtualDevFileSpi,
	KernelVirtualDevFileURandom,
	KernelVirtualDevFileZero,
} KernelVirtualDevFile;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void kernelSetState(KernelState newState);

void kernelShutdownNext(void);

void kernelBoot(void);
void kernelShutdownFinal(void);

void kernelHalt(void);

uint32_t kernelProgmemGenericFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
KernelFsFileOffset kernelProgmemGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);

uint32_t kernelEepromGenericFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
KernelFsFileOffset kernelEepromGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelEepromGenericWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);

uint32_t kernelTmpFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
KernelFsFileOffset kernelTmpReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelTmpWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);
uint16_t kernelTmpMiniFsWriteFunctor(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

uint32_t kernelVirtualDevFileGenericFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);

uint32_t kernelDevDigitalPinFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr);
int16_t kernelDevDigitalPinReadFunctor(void *userData);
bool kernelDevDigitalPinCanReadFunctor(void *userData);
KernelFsFileOffset kernelDevDigitalPinWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);
bool kernelDevDigitalPinCanWriteFunctor(void *userData);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

#ifdef ARDUINO
int main(void) {
#else
int main(int argc, char **argv) {
	// Handle arguments
	kernelFlagProfile=false;
	for(int i=1; i<argc; ++i) {
		if (strcmp(argv[i], "--profile")==0)
			kernelFlagProfile=true;
		else
			printf("Warning: Unknown option '%s'\n", argv[i]);
	}
#endif

	// Init
	kernelBoot();

	// Run processes
	kernelSetState(KernelStateRunning);
	while(procManGetProcessCount()>0) {
		// If we are shutting down, check for all relevant processes dead or a timeout
		if (kernelGetState()==KernelStateShuttingDownWaitAll &&
			(procManGetProcessCount()==1 || ktimeGetMonotonicMs()-kernelStateTime>=30000u)) // 30s timeout
			kernelShutdownNext();

		if (kernelGetState()==KernelStateShuttingDownWaitInit && ktimeGetMonotonicMs()-kernelStateTime>=30000u) // 30s timeout
			break; // break to call shutdown final

		// Check for /dev/ttyS0 updates
		ttyTick();

		// Run hardware device tick functions.
		hwDeviceTick();

		// Run each process for 1 tick, and delay if we have spare time (PC wrapper only - pointless on Arduino)
		#ifndef ARDUINO
		KTime t=ktimeGetMonotonicMs();
		#endif

		procManTickAll();

		#ifndef ARDUINO
		t=ktimeGetMonotonicMs()-t;
		if (t<kernelTickMinTimeMs)
			ktimeDelayMs(kernelTickMinTimeMs-t);
		#endif
	}

	// Quit
	kernelShutdownFinal();

	return 0;
}

void kernelShutdownBegin(void) {
	// Already shutting down?
	if (kernelGetState()>KernelStateRunning)
		return;

	// Send suicide signals to all process except init
	kernelSetState(KernelStateShuttingDownWaitAll);
	kernelLog(LogTypeInfo, kstrP("shutdown request, sending suicide signal to all processes except init\n"));

	for(ProcManPid pid=1; pid<ProcManPidMax; ++pid) {
		if (!procManProcessExists(pid))
			continue;

		procManProcessSendSignal(pid, BytecodeSignalIdSuicide);
	}

	// Return to let main loop wait for processes to die or a timeout to occur
}

KernelState kernelGetState(void) {
	return kernelState;
}

bool kernelSpiGrabLock(uint8_t slaveSelectPin) {
	// Already locked by kernel?
	if (kernelSpiLockFd!=KernelFsFdInvalid)
		return false;

	// Attempt to open file
	kernelSpiLockFd=kernelFsFileOpen("/dev/spi", KernelFsFdModeRW);
	if (kernelSpiLockFd==KernelFsFdInvalid)
		return false;

	// Set slave select pin low to active.
	if (!pinWrite(slaveSelectPin, false)) {
		kernelFsFileClose(kernelSpiLockFd);
		kernelSpiLockFd=KernelFsFdInvalid;
		return false;
	}

	kernelSpiSlaveSelectPin=slaveSelectPin;

	return true;
}

bool kernelSpiGrabLockNoSlaveSelect(void) {
	// Already locked by kernel?
	if (kernelSpiLockFd!=KernelFsFdInvalid)
		return false;

	// Attempt to open file
	kernelSpiLockFd=kernelFsFileOpen("/dev/spi", KernelFsFdModeRW);
	if (kernelSpiLockFd==KernelFsFdInvalid)
		return false;

	// Set slave pin to 255 to indicate no such pin.
	kernelSpiSlaveSelectPin=255;

	return true;
}

void kernelSpiReleaseLock(void) {
	// Not even locked by kernel?
	if (kernelSpiLockFd==KernelFsFdInvalid)
		return;

	// Set slave select pin high to deactive (if any).
	if (kernelSpiSlaveSelectPin!=255)
		pinWrite(kernelSpiSlaveSelectPin, true);

	// Close file
	kernelFsFileClose(kernelSpiLockFd);

	// Clear global fd
	kernelSpiLockFd=KernelFsFdInvalid;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void kernelSetState(KernelState newState) {
	kernelState=newState;
	kernelStateTime=ktimeGetMonotonicMs();
}

void kernelShutdownNext(void) {
	assert(kernelGetState()==KernelStateShuttingDownWaitAll);

	// Forcibly kill any processes who did not commit suicide in time
	bool late=false;
	for(ProcManPid pid=1; pid<ProcManPidMax; ++pid)
		if (procManProcessExists(pid)) {
			late=true;
			break;
		}
	if (late) {
		kernelLog(LogTypeInfo, kstrP("shutdown request, killing processes (except init) which did not commit suicide soon enough\n"));
		for(ProcManPid pid=1; pid<ProcManPidMax; ++pid)
			if (procManProcessExists(pid))
				procManProcessKill(pid, ProcManExitStatusKilled, NULL);
	} else
		kernelLog(LogTypeInfo, kstrP("shutdown request, all processes (except init) committed suicide soon enough\n"));

	// Send suicide signal to init
	kernelSetState(KernelStateShuttingDownWaitInit);
	kernelLog(LogTypeInfo, kstrP("shutdown request, sending suicide signal to init\n"));
	procManProcessSendSignal(0, BytecodeSignalIdSuicide);
}

void kernelBoot(void) {
	// Set logging level to warnings and errors only
	kernelLogSetLevel(LogLevelWarning);

	// Init SPI bus (ready to map to /dev/spi).
	if (!spiInit(SpiClockSpeedDiv64))
		kernelFatalError(kstrP("could not initialise SPI\n"));

	// Init terminal/serial interface (connected to /dev/ttyS0)
	if (!ttyInit())
		kernelFatalError(kstrP("could not initialise TTY\n"));

	// Initialise HW device pins
	// Any pins which are not reserved by this point are considered general purpose for the user
	hwDeviceInit();

	// Enter booting state
	kernelSetState(KernelStateBooting);
	kernelLog(LogTypeInfo, kstrP("booting\n"));

	ktimeInit();

	// Initialise progmem data
	commonProgmemInit();

	// Non-arduino-only: create pretend EEPROM storage in a local file
#ifndef ARDUINO
	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "a+");
	if (kernelFakeEepromFile==NULL)
		kernelFatalError(kstrP("could not open/create pseudo EEPROM storage file at '%s' (PC wrapper)\n"), kernelFakeEepromPath);
	fclose(kernelFakeEepromFile);

	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "r+");
	if (kernelFakeEepromFile==NULL)
		kernelFatalError(kstrP("could not open pseudo EEPROM storage file at '%s' for reading and writing (PC wrapper)\n"), kernelFakeEepromPath);
	fseek(kernelFakeEepromFile, 0L, SEEK_END);
	KernelFsFileOffset eepromInitialSize=ftell(kernelFakeEepromFile);

	while(eepromInitialSize<KernelEepromTotalSize) {
		fputc(0xFF, kernelFakeEepromFile);
		++eepromInitialSize;
	}

	kernelLog(LogTypeInfo, kstrP("openned pseudo EEPROM storage file (PC wrapper)\n"));
#endif

	// Format RAM used for /tmp
	if (!miniFsFormat(&kernelTmpMiniFsWriteFunctor, NULL, KernelTmpDataPoolSize))
		kernelFatalError(kstrP("could not format /tmp volume\n"));
	kernelLog(LogTypeInfo, kstrP("formatted volume representing /tmp (size %u)\n"), KernelTmpDataPoolSize);

	// Init file system and add virtual devices
	kernelFsInit();
	bool error;

	// ... base directories
	error=false;
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/dev"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/lib"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/lib/std"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/media"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/usr"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/usr/man"));
	if (error)
		kernelFatalError(kstrP("fs init failure: base directories\n"));

	// ... essential: tmp directory used for ram
	if (!kernelFsAddBlockDeviceFile(kstrP("/tmp"), &kernelTmpFsFunctor, NULL, KernelFsBlockDeviceFormatCustomMiniFs, KernelTmpDataPoolSize, true))
		kernelFatalError(kstrP("fs init failure: /tmp\n"));

	// ... RO volumes
	bool progmemError=false;
	for(unsigned i=0; i<commonProgmemCount; ++i) {
		progmemError|=!kernelFsAddBlockDeviceFile(commonProgmemData[i].mountPoint, kernelProgmemGenericFsFunctor, &commonProgmemData[i].dataPtr, KernelFsBlockDeviceFormatCustomMiniFs, commonProgmemData[i].size, false);
	}
	if (progmemError)
		kernelLog(LogTypeWarning, kstrP("fs init failure: RO PROGMEM volume error\n"));

	// ... optional EEPROM volumes
	error=false;
	error|=!kernelFsAddBlockDeviceFile(kstrP("/dev/eeprom"), &kernelEepromGenericFsFunctor, (void *)(uintptr_t)KernelEepromDevEepromOffset, KernelFsBlockDeviceFormatFlatFile, KernelEepromDevEepromSize, true);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/etc"), &kernelEepromGenericFsFunctor, (void *)(uintptr_t)KernelEepromEtcOffset, KernelFsBlockDeviceFormatCustomMiniFs, KernelEepromEtcSize, true);
	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /etc and /home\n"));

	// ... optional device files
	error=false;
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/full"), &kernelVirtualDevFileGenericFsFunctor, (void *)(uintptr_t)KernelVirtualDevFileFull, true, true);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/null"), &kernelVirtualDevFileGenericFsFunctor, (void *)(uintptr_t)KernelVirtualDevFileNull, true, true);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/ttyS0"), &kernelVirtualDevFileGenericFsFunctor, (void *)(uintptr_t)KernelVirtualDevFileTtyS0, true, true);
#ifdef ARDUINO
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/spi"), &kernelVirtualDevFileGenericFsFunctor, (void *)(uintptr_t)KernelVirtualDevFileSpi, false, true);
#endif
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/urandom"), &kernelVirtualDevFileGenericFsFunctor, (void *)(uintptr_t)KernelVirtualDevFileURandom, true, false);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/zero"), &kernelVirtualDevFileGenericFsFunctor, (void *)(uintptr_t)KernelVirtualDevFileZero, true, true);

	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /dev\n"));

	// ... optional pin device files
#define ADDDEVDIGITALPIN(path,pinNum) (pinIsValid(pinNum) ? (pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP(path), &kernelDevDigitalPinFsFunctor, (void *)(uintptr_t)(pinNum), false, true),++pinsTarget) : 0)

	uint8_t pinsAdded=0, pinsTarget=0;
	ADDDEVDIGITALPIN("/dev/pin0", 0);
	ADDDEVDIGITALPIN("/dev/pin1", 1);
	ADDDEVDIGITALPIN("/dev/pin2", 2);
	ADDDEVDIGITALPIN("/dev/pin3", 3);
	ADDDEVDIGITALPIN("/dev/pin4", 4);
	ADDDEVDIGITALPIN("/dev/pin5", 5);
	ADDDEVDIGITALPIN("/dev/pin6", 6);
	ADDDEVDIGITALPIN("/dev/pin7", 7);
	ADDDEVDIGITALPIN("/dev/pin8", 8);
	ADDDEVDIGITALPIN("/dev/pin9", 9);
	ADDDEVDIGITALPIN("/dev/pin10", 10);
	ADDDEVDIGITALPIN("/dev/pin11", 11);
	ADDDEVDIGITALPIN("/dev/pin12", 12);
	ADDDEVDIGITALPIN("/dev/pin13", 13);
	ADDDEVDIGITALPIN("/dev/pin14", 14);
	ADDDEVDIGITALPIN("/dev/pin15", 15);
	ADDDEVDIGITALPIN("/dev/pin16", 16);
	ADDDEVDIGITALPIN("/dev/pin17", 17);
	ADDDEVDIGITALPIN("/dev/pin18", 18);
	ADDDEVDIGITALPIN("/dev/pin19", 19);
	ADDDEVDIGITALPIN("/dev/pin20", 20);
	ADDDEVDIGITALPIN("/dev/pin21", 21);
	ADDDEVDIGITALPIN("/dev/pin22", 22);
	ADDDEVDIGITALPIN("/dev/pin23", 23);
	ADDDEVDIGITALPIN("/dev/pin24", 24);
	ADDDEVDIGITALPIN("/dev/pin25", 25);
	ADDDEVDIGITALPIN("/dev/pin26", 26);
	ADDDEVDIGITALPIN("/dev/pin27", 27);
	ADDDEVDIGITALPIN("/dev/pin28", 28);
	ADDDEVDIGITALPIN("/dev/pin29", 29);
	ADDDEVDIGITALPIN("/dev/pin30", 30);
	ADDDEVDIGITALPIN("/dev/pin31", 31);
	ADDDEVDIGITALPIN("/dev/pin32", 32);
	ADDDEVDIGITALPIN("/dev/pin33", 33);
	ADDDEVDIGITALPIN("/dev/pin34", 34);
	ADDDEVDIGITALPIN("/dev/pin35", 35);
	ADDDEVDIGITALPIN("/dev/pin36", 36);
	ADDDEVDIGITALPIN("/dev/pin37", 37);
	ADDDEVDIGITALPIN("/dev/pin38", 38);
	ADDDEVDIGITALPIN("/dev/pin39", 39);
	ADDDEVDIGITALPIN("/dev/pin40", 40);
	ADDDEVDIGITALPIN("/dev/pin41", 41);
	ADDDEVDIGITALPIN("/dev/pin42", 42);
	ADDDEVDIGITALPIN("/dev/pin43", 43);
	ADDDEVDIGITALPIN("/dev/pin44", 44);
	ADDDEVDIGITALPIN("/dev/pin45", 45);
	ADDDEVDIGITALPIN("/dev/pin46", 46);
	ADDDEVDIGITALPIN("/dev/pin47", 47);
	ADDDEVDIGITALPIN("/dev/pin48", 48);
	ADDDEVDIGITALPIN("/dev/pin49", 49);
	ADDDEVDIGITALPIN("/dev/pin50", 50);
	ADDDEVDIGITALPIN("/dev/pin51", 51);
	ADDDEVDIGITALPIN("/dev/pin52", 52);
	ADDDEVDIGITALPIN("/dev/pin53", 53);
	ADDDEVDIGITALPIN("/dev/pin54", 54);
	ADDDEVDIGITALPIN("/dev/pin55", 55);
	ADDDEVDIGITALPIN("/dev/pin56", 56);
	ADDDEVDIGITALPIN("/dev/pin57", 57);
	ADDDEVDIGITALPIN("/dev/pin58", 58);
	ADDDEVDIGITALPIN("/dev/pin59", 59);
	ADDDEVDIGITALPIN("/dev/pin60", 60);
	ADDDEVDIGITALPIN("/dev/pin61", 61);
	ADDDEVDIGITALPIN("/dev/pin62", 62);
	ADDDEVDIGITALPIN("/dev/pin63", 63);
	ADDDEVDIGITALPIN("/dev/pin64", 64);
	ADDDEVDIGITALPIN("/dev/pin65", 65);
	ADDDEVDIGITALPIN("/dev/pin66", 66);
	ADDDEVDIGITALPIN("/dev/pin67", 67);
	ADDDEVDIGITALPIN("/dev/pin68", 68);
	ADDDEVDIGITALPIN("/dev/pin69", 69);
	ADDDEVDIGITALPIN("/dev/pin70", 70);
	ADDDEVDIGITALPIN("/dev/pin71", 71);
	ADDDEVDIGITALPIN("/dev/pin72", 72);
	ADDDEVDIGITALPIN("/dev/pin73", 73);
	ADDDEVDIGITALPIN("/dev/pin74", 74);
	ADDDEVDIGITALPIN("/dev/pin75", 75);
	ADDDEVDIGITALPIN("/dev/pin76", 76);
	ADDDEVDIGITALPIN("/dev/pin77", 77);
	ADDDEVDIGITALPIN("/dev/pin78", 78);
	ADDDEVDIGITALPIN("/dev/pin79", 79);
	ADDDEVDIGITALPIN("/dev/pin80", 80);
	ADDDEVDIGITALPIN("/dev/pin81", 81);
	ADDDEVDIGITALPIN("/dev/pin82", 82);
	ADDDEVDIGITALPIN("/dev/pin83", 83);
	ADDDEVDIGITALPIN("/dev/pin84", 84);
	ADDDEVDIGITALPIN("/dev/pin85", 85);
	ADDDEVDIGITALPIN("/dev/pin86", 86);
	ADDDEVDIGITALPIN("/dev/pin87", 87);
	ADDDEVDIGITALPIN("/dev/pin88", 88);
	ADDDEVDIGITALPIN("/dev/pin89", 89);
	ADDDEVDIGITALPIN("/dev/pin90", 90);
	ADDDEVDIGITALPIN("/dev/pin91", 91);
	ADDDEVDIGITALPIN("/dev/pin92", 92);
	ADDDEVDIGITALPIN("/dev/pin93", 93);
	ADDDEVDIGITALPIN("/dev/pin94", 94);
	ADDDEVDIGITALPIN("/dev/pin95", 95);
	ADDDEVDIGITALPIN("/dev/pin96", 96);
	ADDDEVDIGITALPIN("/dev/pin97", 97);
	ADDDEVDIGITALPIN("/dev/pin98", 98);
	ADDDEVDIGITALPIN("/dev/pin99", 99);

	kernelLog((pinsAdded==pinsTarget ? LogTypeInfo : LogTypeWarning), kstrP("added %u/%u digital pin devices\n"), pinsAdded, pinsTarget);

#undef ADDDEVDIGITALPIN

	kernelLog(LogTypeInfo, kstrP("initialised filesystem\n"));

	// Initialise process manager and start init process
	procManInit();
	kernelLog(LogTypeInfo, kstrP("initialised process manager\n"));

	kernelLog(LogTypeInfo, kstrP("starting init\n"));
	if (procManProcessNew("/bin/init")==ProcManPidMax)
		kernelFatalError(kstrP("could not start init at '%s'\n"), "/bin/init");

	kernelLog(LogTypeInfo, kstrP("booting complete\n"));
}

void kernelShutdownFinal(void) {
	kernelSetState(KernelStateShuttingDownFinal);
	kernelLog(LogTypeInfo, kstrP("shutting down final\n"));

	// Quit process manager
	kernelLog(LogTypeInfo, kstrP("killing process manager\n"));
	procManQuit();

	// Quit file system
	kernelLog(LogTypeInfo, kstrP("unmounting filesystem\n"));
	kernelFsQuit();

	// Non-arduino-only: close pretend EEPROM storage file
#ifndef ARDUINO
	kernelLog(LogTypeInfo, kstrP("closing pseudo EEPROM storage file (PC wrapper)\n"));
	fclose(kernelFakeEepromFile);
#endif

	// Reset tty stuff
	ttyQuit();

	// Halt
	kernelHalt();
}

void kernelHalt(void) {
	kernelSetState(KernelStateShutdown);

#ifdef ARDUINO
	kernelLog(LogTypeInfo, kstrP("halting\n"));
	while(1)
		;
#else
	kernelLog(LogTypeInfo, kstrP("exiting (PC wrapper)\n"));
	exit(0);
#endif
}

uint32_t kernelProgmemGenericFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return true;
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
			return kernelProgmemGenericReadFunctor(addr, data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
		break;
	}

	assert(false);
	return 0;
}

KernelFsFileOffset kernelProgmemGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
#ifdef ARDUINO
	uint32_t farAddr=*(const uint32_t *)userData;
	KernelFsFileOffset i;
	for(i=0; i<len; ++i)
		data[i]=pgm_read_byte_far(farAddr+addr+i);
#else
	const uintptr_t dataSourceAddr=*(const uintptr_t *)userData;
	const uint8_t *dataSource=(const uint8_t *)dataSourceAddr;
	memcpy(data, dataSource+addr, len);
#endif
	return len;
}

uint32_t kernelEepromGenericFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return true;
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
			return kernelEepromGenericReadFunctor(addr, data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
			return kernelEepromGenericWriteFunctor(addr, data, len, userData);
		break;
	}

	assert(false);
	return 0;
}

KernelFsFileOffset kernelEepromGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	uint16_t offset=(uint16_t)(uintptr_t)userData;
	addr+=offset;
	if (addr>UINT16_MAX)
		return 0;

#ifdef ARDUINO
	eeprom_read_block(data, (void *)(uint16_t)addr, len);
	return len;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %"PRIu32" in generic EEPROM read functor (offset=%u)\n"), addr, offset);
		return 0;
	}
	KernelFsFileOffset result=fread(data, 1, len, kernelFakeEepromFile);
	if (result!=len)
		kernelLog(LogTypeWarning, kstrP("could not read at addr %"PRIu32" in generic EEPROM read functor (offset=%u, result=%u)\n"), addr, offset, result);
	return result;
#endif
}

KernelFsFileOffset kernelEepromGenericWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	if (len>UINT16_MAX)
		len=UINT16_MAX;
	uint16_t offset=(uint16_t)(uintptr_t)userData;
	addr+=offset;
	if (addr>UINT16_MAX)
		return 0;

#ifdef ARDUINO
	eeprom_update_block((const void *)data, (void *)(uint16_t)addr, len);
	return len;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %"PRIu32" in /dev/eeprom write functor (offset=%i)\n"), addr, offset);
		return 0;
	}
	KernelFsFileOffset result=fwrite(data, 1, len, kernelFakeEepromFile);
	if (result!=len)
		kernelLog(LogTypeWarning, kstrP("could not write to addr %"PRIu32" in /dev/eeprom write functor (offset=%i,result=%i)\n"), addr, offset, result);
	return result;
#endif
}

uint32_t kernelTmpFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return true;
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
			return kernelTmpReadFunctor(addr, data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
			return kernelTmpWriteFunctor(addr, data, len, userData);
		break;
	}

	assert(false);
	return 0;
}

KernelFsFileOffset kernelTmpReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	memcpy(data, kernelTmpDataPool+addr, len);
	return len;
}

KernelFsFileOffset kernelTmpWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	memcpy(kernelTmpDataPool+addr, data, len);
	return len;
}

uint16_t kernelTmpMiniFsWriteFunctor(uint16_t addr, const uint8_t *data, uint16_t len, void *userData) {
	return kernelTmpWriteFunctor(addr, data, len, userData);
}

uint32_t kernelVirtualDevFileGenericFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	KernelVirtualDevFile file=(KernelVirtualDevFile)(uintptr_t)userData;
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return true;
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
			switch(file) {
				case KernelVirtualDevFileFull:
					return 0;
				break;
				case KernelVirtualDevFileNull:
					return 0;
				break;
				case KernelVirtualDevFileTtyS0:
					return ttyReadFunctor();
				break;
				case KernelVirtualDevFileSpi:
					return spiReadByte();
				break;
				case KernelVirtualDevFileURandom:
					return rand()&0xFF;
				break;
				case KernelVirtualDevFileZero:
					return 0;
				break;
			}
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
			switch(file) {
				case KernelVirtualDevFileFull:
					return true;
				break;
				case KernelVirtualDevFileNull:
					return true;
				break;
				case KernelVirtualDevFileTtyS0:
					return ttyCanReadFunctor();
				break;
				case KernelVirtualDevFileSpi:
					return true;
				break;
				case KernelVirtualDevFileURandom:
					return true;
				break;
				case KernelVirtualDevFileZero:
					return true;
				break;
			}
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
			switch(file) {
				case KernelVirtualDevFileFull:
					return 0;
				break;
				case KernelVirtualDevFileNull:
					return len;
				break;
				case KernelVirtualDevFileTtyS0:
					return ttyWriteFunctor(data, len);
				break;
				case KernelVirtualDevFileSpi:
					if (len>UINT16_MAX)
						len=UINT16_MAX;
					spiWriteBlock(data, len);
					return len;
				break;
				case KernelVirtualDevFileURandom:
					return 0;
				break;
				case KernelVirtualDevFileZero:
					return len;
				break;
			}
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
			switch(file) {
				case KernelVirtualDevFileFull:
					return false;
				break;
				case KernelVirtualDevFileNull:
					return true;
				break;
				case KernelVirtualDevFileTtyS0:
					return ttyCanWriteFunctor();
				break;
				case KernelVirtualDevFileSpi:
					return true;
				break;
				case KernelVirtualDevFileURandom:
					return false;
				break;
				case KernelVirtualDevFileZero:
					return true;
				break;
			}
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
		break;
	}

	assert(false);
	return 0;
}

uint32_t kernelDevDigitalPinFsFunctor(KernelFsDeviceFunctorType type, void *userData, uint8_t *data, KernelFsFileOffset len, KernelFsFileOffset addr) {
	switch(type) {
		case KernelFsDeviceFunctorTypeCommonFlush:
			return true;
		break;
		case KernelFsDeviceFunctorTypeCharacterRead:
			return kernelDevDigitalPinReadFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterCanRead:
			return kernelDevDigitalPinCanReadFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterWrite:
			return kernelDevDigitalPinWriteFunctor(data, len, userData);
		break;
		case KernelFsDeviceFunctorTypeCharacterCanWrite:
			return kernelDevDigitalPinCanWriteFunctor(userData);
		break;
		case KernelFsDeviceFunctorTypeBlockRead:
		break;
		case KernelFsDeviceFunctorTypeBlockWrite:
		break;
	}

	assert(false);
	return 0;
}

int16_t kernelDevDigitalPinReadFunctor(void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;
	return pinRead(pinNum);
}

bool kernelDevDigitalPinCanReadFunctor(void *userData) {
	return true;
}

KernelFsFileOffset kernelDevDigitalPinWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;

	// Forbid writes from user space to HW device pins (unless associated device has type HwDeviceTypeRaw).
	HwDeviceId hwDeviceId=hwDeviceGetDeviceForPin(pinNum);
	if (hwDeviceId!=HwDeviceIdMax && hwDeviceGetType(hwDeviceId)!=HwDeviceTypeRaw)
		return 0;

	// Simply write last of values given (considered as a boolean),
	// acting as if we had looped over and set each state in turn.
	if (pinWrite(pinNum, (data[len-1]!=0)))
		return len;
	else
		return 0;
}

bool kernelDevDigitalPinCanWriteFunctor(void *userData) {
	return true;
}
