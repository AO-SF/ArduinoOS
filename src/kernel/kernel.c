#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include "circbuf.h"
#include "uart.h"
#else
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "kernel.h"
#include "kernelfs.h"
#include "log.h"
#include "minifs.h"
#include "pins.h"
#include "procman.h"
#include "ktime.h"
#include "util.h"

#include "commonprogmem.h"

#ifdef KERNELCUSTOMRAMSIZE
#define KernelTmpDataPoolSize KERNELCUSTOMRAMSIZE
#else
#define KernelTmpDataPoolSize (2*1024) // 2kb - used as ram
#endif
uint8_t kernelTmpDataPool[KernelTmpDataPoolSize];

#define KernelEepromTotalSize (4*1024) // Mega has 4kb for example
#define KernelEepromEtcOffset (0)
#define KernelEepromEtcSize (1*1024)
#define KernelEepromDevEepromOffset KernelEepromEtcSize
#define KernelEepromDevEepromSize (KernelEepromTotalSize-KernelEepromDevEepromOffset)

#ifdef ARDUINO
volatile bool kernelDevTtyS0EchoFlag;
volatile bool kernelDevTtyS0BlockingFlag;
volatile CircBuf kernelDevTtyS0CircBuf;
volatile uint8_t kernelDevTtyS0CircBufNewlineCount;
#else
const char *kernelFakeEepromPath="./eeprom";
FILE *kernelFakeEepromFile=NULL;

int kernelTtyS0BytesAvailable=0; // We have to store this to avoid polling too often causing us to think no data is waiting

#endif

volatile uint8_t kernelCtrlCWaiting=false;

// reader pid array stores pid of processes with /dev/ttyS0 open, used for ctrl+c propagation from host
#define kernelReaderPidArrayMax 4
ProcManPid kernelReaderPidArray[kernelReaderPidArrayMax];

KernelState kernelState=KernelStateInvalid;
uint32_t kernelStateTime=0;

#define kernelFatalError(format, ...) do { kernelLog(LogTypeError, format, ##__VA_ARGS__); kernelHalt(); } while(0)

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void kernelSetState(KernelState newState);

void kernelShutdownNext(void);

void kernelBoot(void);
void kernelShutdownFinal(void);

void kernelHalt(void);

KernelFsFileOffset kernelProgmemGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelEepromGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelEepromGenericWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelTmpReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData);
KernelFsFileOffset kernelTmpWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelDevZeroReadFunctor(void *userData);
bool kernelDevZeroCanReadFunctor(void *userData);
KernelFsFileOffset kernelDevZeroWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelDevFullReadFunctor(void *userData);
bool kernelDevFullCanReadFunctor(void *userData);
KernelFsFileOffset kernelDevFullWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelDevNullReadFunctor(void *userData);
bool kernelDevNullCanReadFunctor(void *userData);
KernelFsFileOffset kernelDevNullWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelDevURandomReadFunctor(void *userData);
bool kernelDevURandomCanReadFunctor(void *userData);
KernelFsFileOffset kernelDevURandomWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelDevTtyS0ReadFunctor(void *userData);
bool kernelDevTtyS0CanReadFunctor(void *userData);
KernelFsFileOffset kernelDevTtyS0WriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);
int16_t kernelDevPinReadFunctor(void *userData);
bool kernelDevPinCanReadFunctor(void *userData);
KernelFsFileOffset kernelDevPinWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData);

#ifndef ARDUINO
void kernelSigIntHandler(int sig);
#endif

void kernelCtrlCStart(void);
void kernelCtrlCSend(void);

#ifdef ARDUINO
#include <avr/io.h>
#include <avr/sleep.h>
ISR(USART0_RX_vect) {
	ATOMIC_BLOCK(ATOMIC_FORCEON) {
		uint8_t value=UDR0;
		if (value==3) {
			// Ctrl+c
			kernelCtrlCStart();
		} else if (value==127) {
			// Backspace - try to remove last char from buffer, unless it is a newline
			uint8_t tailValue;
			if (circBufTailPeek(&kernelDevTtyS0CircBuf, &tailValue)) {
				if (tailValue!='\n' && circBufUnpush(&kernelDevTtyS0CircBuf)) {
					// Clear last char on screen
					const uint8_t tempChars[3]={8,' ',8};
					kernelDevTtyS0WriteFunctor(tempChars, 3, NULL);
				}
			}
		} else {
			// Standard character
			if (value=='\r')
				value='\n'; // HACK
			circBufPush(&kernelDevTtyS0CircBuf, value);
			if (value=='\n')
				++kernelDevTtyS0CircBufNewlineCount;
			if (kernelDevTtyS0EchoFlag)
				kernelDevTtyS0WriteFunctor(&value, 1, NULL);
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

int main(void) {
	// Init
	kernelBoot();

	// Run processes
	kernelSetState(KernelStateRunning);
	while(procManGetProcessCount()>0) {
		// If we are shutting down, check for all relevant processes dead or a timeout
		if (kernelGetState()==KernelStateShuttingDownWaitAll &&
			(procManGetProcessCount()==1 || ktimeGetMs()-kernelStateTime>=30000u)) // 30s timeout
			kernelShutdownNext();

		if (kernelGetState()==KernelStateShuttingDownWaitInit && ktimeGetMs()-kernelStateTime>=30000u) // 30s timeout
			break; // break to call shutdown final

		// Check for ctrl+c to propagate
		kernelCtrlCSend();

		// Run each process for 1 tick, and delay if we have spare time (PC wrapper only - pointless on Arduino)
		#ifndef ARDUINO
		uint32_t t=ktimeGetMs();
		#endif
		procManTickAll();
		#ifndef ARDUINO
		t=ktimeGetMs()-t;
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

bool kernelReaderPidCanAdd(void) {
	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i)
		if (kernelReaderPidArray[i]==ProcManPidMax)
			return true;
	return false;
}
bool kernelReaderPidAdd(ProcManPid pid) {
	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i)
		if (kernelReaderPidArray[i]==ProcManPidMax) {
			kernelReaderPidArray[i]=pid;
			return true;
		}
	return false;
}

bool kernelReaderPidRemove(ProcManPid pid) {
	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i)
		if (kernelReaderPidArray[i]==pid) {
			kernelReaderPidArray[i]=ProcManPidMax;
			return true;
		}
	return false;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void kernelSetState(KernelState newState) {
	kernelState=newState;
	kernelStateTime=ktimeGetMs();
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
				procManProcessKill(pid, ProcManExitStatusKilled);
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

	// Initialise reader PID array to indicate that no processes currently have /dev/ttyS0 open
	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i)
		kernelReaderPidArray[i]=ProcManPidMax;

	// Arduino-only: init uart for serial (for kernel logging, and ready to map to /dev/ttyS0).
#ifdef ARDUINO
	kernelDevTtyS0EchoFlag=true;
	kernelDevTtyS0CircBufNewlineCount=0;
	kernelDevTtyS0BlockingFlag=true;
	circBufInit(&kernelDevTtyS0CircBuf);
	uart_init();

	stdout=&uart_output;
	stderr=&uart_output;
	stdin=&uart_input;

	cli();
	UCSR0B=(1<<RXEN0)|(1<<TXEN0)|(1<<RXCIE0);
	set_sleep_mode(SLEEP_MODE_IDLE);
	sei();

	kernelLog(LogTypeInfo, kstrP("initialised uart (serial)\n"));
#endif

	// Enter booting state
	kernelSetState(KernelStateBooting);
	kernelLog(LogTypeInfo, kstrP("booting\n"));

	ktimeInit();

	// Initialise progmem data
	commonProgmemInit();

	// PC only - register sigint handler so we can pass this signal onto e.g. the shell
#ifndef ARDUINO
    signal(SIGINT, kernelSigIntHandler); // TODO: Check return.
#endif

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
	if (!miniFsFormat(&kernelTmpWriteFunctor, NULL, KernelTmpDataPoolSize))
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
	if (!kernelFsAddBlockDeviceFile(kstrP("/tmp"), KernelFsBlockDeviceFormatCustomMiniFs, KernelTmpDataPoolSize, &kernelTmpReadFunctor, &kernelTmpWriteFunctor, NULL))
		kernelFatalError(kstrP("fs init failure: /tmp\n"));

	// ... RO volumes
	bool progmemError=false;
	for(unsigned i=0; i<commonProgmemCount; ++i) {
		progmemError|=!kernelFsAddBlockDeviceFile(commonProgmemData[i].mountPoint, KernelFsBlockDeviceFormatCustomMiniFs, commonProgmemData[i].size, &kernelProgmemGenericReadFunctor, NULL, &commonProgmemData[i].dataPtr);
	}
	if (progmemError)
		kernelLog(LogTypeWarning, kstrP("fs init failure: RO PROGMEM volume error\n"));

	// ... optional EEPROM volumes
	error=false;
	error|=!kernelFsAddBlockDeviceFile(kstrP("/dev/eeprom"), KernelFsBlockDeviceFormatFlatFile, KernelEepromDevEepromSize, &kernelEepromGenericReadFunctor, &kernelEepromGenericWriteFunctor, (void *)(uintptr_t)KernelEepromDevEepromOffset);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/etc"), KernelFsBlockDeviceFormatCustomMiniFs, KernelEepromEtcSize, &kernelEepromGenericReadFunctor, &kernelEepromGenericWriteFunctor, (void *)(uintptr_t)KernelEepromEtcOffset);
	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /etc and /home\n"));

	// ... optional device files
	error=false;
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/full"), &kernelDevFullReadFunctor, &kernelDevFullCanReadFunctor, &kernelDevFullWriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/null"), &kernelDevNullReadFunctor, &kernelDevNullCanReadFunctor, &kernelDevNullWriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/ttyS0"), &kernelDevTtyS0ReadFunctor, &kernelDevTtyS0CanReadFunctor, &kernelDevTtyS0WriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/urandom"), &kernelDevURandomReadFunctor, &kernelDevURandomCanReadFunctor, &kernelDevURandomWriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/zero"), &kernelDevZeroReadFunctor, &kernelDevZeroCanReadFunctor, &kernelDevZeroWriteFunctor, true, NULL);

	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /dev\n"));

	// ... optional pin device files
	// TODO: include all once we figure out how to fit into ram
	// For now just include digital pins 2 through 13 (avoiding serial pins 0 & 1, and including led pin at 13)
	uint8_t pinsAdded=0;
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin12"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD10);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin13"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD11);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin14"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD12);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin15"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD13);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin35"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD5);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin36"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD2);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin37"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD3);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin53"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD4);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin59"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD6);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin60"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD7);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin61"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD8);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin62"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD9);

	const uint8_t pinsTarget=13-2+1;
	kernelLog((pinsAdded==pinsTarget ? LogTypeInfo : LogTypeWarning), kstrP("added %u/%u pin devices\n"), pinsAdded, pinsTarget);

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
	fclose(kernelFakeEepromFile); // TODO: Check return
#endif

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

KernelFsFileOffset kernelEepromGenericReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	KernelFsFileOffset offset=(KernelFsFileOffset)(uintptr_t)userData;
	addr+=offset;

#ifdef ARDUINO
	eeprom_read_block(data, (void *)addr, len);
	return len;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %u in generic EEPROM read functor (offset=%u)\n"), addr, offset);
		return 0;
	}
	KernelFsFileOffset result=fread(data, 1, len, kernelFakeEepromFile);
	if (result!=len)
		kernelLog(LogTypeWarning, kstrP("could not read at addr %u in generic EEPROM read functor (offset=%u, result=%u)\n"), addr, offset, result);
	return result;
#endif
}

KernelFsFileOffset kernelEepromGenericWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	KernelFsFileOffset offset=(KernelFsFileOffset)(uintptr_t)userData;
	addr+=offset;

#ifdef ARDUINO
	eeprom_update_block((const void *)data, (void *)addr, len);
	return len;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %u in /dev/eeprom write functor (offset=%i)\n"), addr, offset);
		return 0;
	}
	KernelFsFileOffset result=fwrite(data, 1, len, kernelFakeEepromFile);
	if (result!=len)
		kernelLog(LogTypeWarning, kstrP("could not write to addr %u in /dev/eeprom write functor (offset=%i,result=%i)\n"), addr, offset, result);
	return result;
#endif
}

KernelFsFileOffset kernelTmpReadFunctor(KernelFsFileOffset addr, uint8_t *data, KernelFsFileOffset len, void *userData) {
	memcpy(data, kernelTmpDataPool+addr, len);
	return len;
}


KernelFsFileOffset kernelTmpWriteFunctor(KernelFsFileOffset addr, const uint8_t *data, KernelFsFileOffset len, void *userData) {
	memcpy(kernelTmpDataPool+addr, data, len);
	return len;
}

int16_t kernelDevZeroReadFunctor(void *userData) {
	return 0;
}

bool kernelDevZeroCanReadFunctor(void *userData) {
	return true;
}


KernelFsFileOffset kernelDevZeroWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	return len;
}

int16_t kernelDevFullReadFunctor(void *userData) {
	return 0;
}

bool kernelDevFullCanReadFunctor(void *userData) {
	return true;
}

KernelFsFileOffset kernelDevFullWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	return 0;
}

int16_t kernelDevNullReadFunctor(void *userData) {
	return 0;
}

bool kernelDevNullCanReadFunctor(void *userData) {
	return true;
}

KernelFsFileOffset kernelDevNullWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	return len;
}

int16_t kernelDevURandomReadFunctor(void *userData) {
	return rand()&0xFF;
}

bool kernelDevURandomCanReadFunctor(void *userData) {
	return true;
}

KernelFsFileOffset kernelDevURandomWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	return 0;
}

int16_t kernelDevTtyS0ReadFunctor(void *userData) {
#ifdef ARDUINO
	int16_t ret=-1;

	if (kernelDevTtyS0CanReadFunctor(userData)) {
		uint8_t value;
		ATOMIC_BLOCK(ATOMIC_FORCEON) {
			if (circBufPop(&kernelDevTtyS0CircBuf, &value)) {
				ret=value;
				if (value=='\n')
					--kernelDevTtyS0CircBufNewlineCount;
			}
		}
	}

	return ret;
#else
	if (kernelTtyS0BytesAvailable==0)
		kernelLog(LogTypeWarning, kstrP("kernelTtyS0BytesAvailable=0 going into kernelDevTtyS0ReadFunctor\n"));
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
	if (kernelDevTtyS0CircBufNewlineCount>0)
		return true;
	if (kernelDevTtyS0BlockingFlag)
		return false;
	return !circBufIsEmpty(&kernelDevTtyS0CircBuf);
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

KernelFsFileOffset kernelDevTtyS0WriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	KernelFsFileOffset written=fwrite(data, 1, len, stdout);
	fflush(stdout);

	// FIXME: KernelFsFileOffset is unsigned so check below is a hack.
	if (written>=UINT16_MAX-256) // should be written<0
		written=0;

	return written;
}

int16_t kernelDevPinReadFunctor(void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;
	return pinRead(pinNum);
}

bool kernelDevPinCanReadFunctor(void *userData) {
	return true;
}

KernelFsFileOffset kernelDevPinWriteFunctor(const uint8_t *data, KernelFsFileOffset len, void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;

	// Simply write last of values given (considered as a boolean),
	// acting as if we had looped over and set each state in turn.
	if (pinWrite(pinNum, (data[len-1]!=0)))
		return len;
	else
		return 0;
}

#ifndef ARDUINO
void kernelSigIntHandler(int sig) {
	kernelCtrlCStart();
}
#endif

void kernelCtrlCStart(void) {
	kernelCtrlCWaiting=true;
}

void kernelCtrlCSend(void) {
	if (!kernelCtrlCWaiting)
		return;

	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i) {
		if (kernelReaderPidArray[i]!=ProcManPidMax) {
			// Avoid double-caling for a single process
			uint8_t j;
			for(j=0; j<i; ++j)
				if (kernelReaderPidArray[j]==kernelReaderPidArray[i])
					break;
			if (j==i)
				procManProcessSendSignal(kernelReaderPidArray[i], BytecodeSignalIdInterrupt);
		}
	}

	kernelCtrlCWaiting=false;
}
