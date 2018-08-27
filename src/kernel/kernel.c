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
#include "wrapper.h"

#include "progmembin.h"
#include "progmemlibcurses.h"
#include "progmemlibpin.h"
#include "progmemlibsys.h"
#include "progmemlibstdio.h"
#include "progmemlibstdmath.h"
#include "progmemlibstdproc.h"
#include "progmemlibstdmem.h"
#include "progmemlibstdstr.h"
#include "progmemlibstdtime.h"
#include "progmemman1.h"
#include "progmemman2.h"
#include "progmemman3.h"
#include "progmemusrgames.h"
#include "progmemusrbin.h"

#define KernelTmpDataPoolSize (2*1024) // 2kb - used as ram
uint8_t kernelTmpDataPool[KernelTmpDataPoolSize];

#define KernelBinSize PROGMEMbinDATASIZE
#define KernelLibCursesSize PROGMEMlibcursesDATASIZE
#define KernelLibPinSize PROGMEMlibpinDATASIZE
#define KernelLibSysSize PROGMEMlibsysDATASIZE
#define KernelLibStdIoSize PROGMEMlibstdioDATASIZE
#define KernelLibStdMathSize PROGMEMlibstdmathDATASIZE
#define KernelLibStdProcSize PROGMEMlibstdprocDATASIZE
#define KernelLibStdMemSize PROGMEMlibstdmemDATASIZE
#define KernelLibStdStrSize PROGMEMlibstdstrDATASIZE
#define KernelLibStdTimeSize PROGMEMlibstdtimeDATASIZE
#define KernelMan1Size PROGMEMman1DATASIZE
#define KernelMan2Size PROGMEMman2DATASIZE
#define KernelMan3Size PROGMEMman3DATASIZE
#define KernelUsrGamesSize PROGMEMusrgamesDATASIZE
#define KernelUsrBinSize PROGMEMusrbinDATASIZE

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

int16_t kernelBinReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelMan1ReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelMan2ReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelMan3ReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibCursesReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibPinReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibSysReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibStdIoReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibStdMathReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibStdProcReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibStdMemReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibStdStrReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelLibStdTimeReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelDevEepromReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelDevEepromWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);
int16_t kernelEtcReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelEtcWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);
uint8_t kernelEtcMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelEtcMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int16_t kernelTmpReadFunctor(KernelFsFileOffset addr, void *userData);
bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData);
uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int16_t kernelDevZeroReadFunctor(void *userData);
bool kernelDevZeroCanReadFunctor(void *userData);
bool kernelDevZeroWriteFunctor(uint8_t value, void *userData);
int16_t kernelDevFullReadFunctor(void *userData);
bool kernelDevFullCanReadFunctor(void *userData);
bool kernelDevFullWriteFunctor(uint8_t value, void *userData);
int16_t kernelDevNullReadFunctor(void *userData);
bool kernelDevNullCanReadFunctor(void *userData);
bool kernelDevNullWriteFunctor(uint8_t value, void *userData);
int16_t kernelDevURandomReadFunctor(void *userData);
bool kernelDevURandomCanReadFunctor(void *userData);
bool kernelDevURandomWriteFunctor(uint8_t value, void *userData);
int16_t kernelDevTtyS0ReadFunctor(void *userData);
bool kernelDevTtyS0CanReadFunctor(void *userData);
bool kernelDevTtyS0WriteFunctor(uint8_t value, void *userData);
int16_t kernelUsrBinReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelUsrGamesReadFunctor(KernelFsFileOffset addr, void *userData);
int16_t kernelDevPinReadFunctor(void *userData);
bool kernelDevPinCanReadFunctor(void *userData);
bool kernelDevPinWriteFunctor(uint8_t value, void *userData);

#ifndef ARDUINO
void kernelSigIntHandler(int sig);
#endif

#ifdef ARDUINO
#include <avr/io.h>
#include <avr/sleep.h>
ISR(USART0_RX_vect) {
	ATOMIC_BLOCK(ATOMIC_FORCEON) {
		uint8_t value=UDR0;
		if (value==3) {
			// Ctrl+c
			if (kernelReaderPid!=ProcManPidMax)
				procManProcessSendSignal(kernelReaderPid, ByteCodeSignalIdInterrupt);
		} else if (value==127) {
			// Backspace - try to remove last char from buffer, unless it is a newline
			uint8_t tailValue;
			if (circBufTailPeek(&kernelDevTtyS0CircBuf, &tailValue)) {
				if (tailValue!='\n' && circBufUnpush(&kernelDevTtyS0CircBuf)) {
					// Clear last char on screen
					kernelDevTtyS0WriteFunctor(8, NULL);
					kernelDevTtyS0WriteFunctor(' ', NULL);
					kernelDevTtyS0WriteFunctor(8, NULL);
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
				kernelDevTtyS0WriteFunctor(value, NULL);
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
			(procManGetProcessCount()==1 || millis()-kernelStateTime>=3000)) // 3s timeout
			kernelShutdownNext();

		if (kernelGetState()==KernelStateShuttingDownWaitInit && millis()-kernelStateTime>=3000) // 3s timeout
			break; // break to call shutdown final

		// Run each process for 1 tick, and delay if we have spare time (PC wrapper only - pointless on Arduino)
		#ifndef ARDUINO
		uint32_t t=millis();
		#endif
		procManTickAll();
		#ifndef ARDUINO
		t=millis()-t;
		if (t<kernelTickMinTimeMs)
			delay(kernelTickMinTimeMs-t);
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

		procManProcessSendSignal(pid, ByteCodeSignalIdSuicide);
	}

	// Return to let main loop wait for processes to die or a  timeout to occur
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
	kernelStateTime=millis();
}

void kernelShutdownNext(void) {
	assert(kernelGetState()==KernelStateShuttingDownWaitAll);

	// Forcibly kill any processes who did not commit suicide in time
	kernelLog(LogTypeInfo, kstrP("shutdown request, killing any processes (except init) which did not commit suicide soon enough\n"));
	for(ProcManPid pid=1; pid<ProcManPidMax; ++pid) {
		if (!procManProcessExists(pid))
			continue;

		procManProcessKill(pid, ProcManExitStatusKilled);
	}

	// Send suicide signal to init
	kernelSetState(KernelStateShuttingDownWaitInit);
	kernelLog(LogTypeInfo, kstrP("shutdown request, sending suicide signal to init\n"));
	procManProcessSendSignal(0, ByteCodeSignalIdSuicide);
}

void kernelBoot(void) {
	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i)
		kernelReaderPidArray[i]=ProcManPidMax;

	// Arduino-only: init uart for serial (for kernel logging, and ready to map to /dev/ttyS0).
#ifdef ARDUINO
	kernelDevTtyS0EchoFlag=true;
	kernelDevTtyS0CircBufNewlineCount=0;
	kernelDevTtyS0BlockingFlag=false;
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

	millisInit();

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
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/media"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/usr"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/usr/man"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/lib"));
	error|=!kernelFsAddDirectoryDeviceFile(kstrP("/lib/std"));
	if (error)
		kernelFatalError(kstrP("fs init failure: base directories\n"));

	// ... essential: tmp directory used for ram
	if (!kernelFsAddBlockDeviceFile(kstrP("/tmp"), KernelFsBlockDeviceFormatCustomMiniFs, KernelTmpDataPoolSize, &kernelTmpReadFunctor, &kernelTmpWriteFunctor, NULL))
		kernelFatalError(kstrP("fs init failure: /tmp\n"));

	// ... essential: RO volume /bin
	if (!kernelFsAddBlockDeviceFile(kstrP("/bin"), KernelFsBlockDeviceFormatCustomMiniFs, KernelBinSize, &kernelBinReadFunctor, NULL, NULL))
		kernelFatalError(kstrP("fs init failure: /bin\n"));

	// ... non-essential RO volumes
	error=false;
	error|=!kernelFsAddBlockDeviceFile(kstrP("/usr/bin"), KernelFsBlockDeviceFormatCustomMiniFs, KernelUsrBinSize, &kernelUsrBinReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/usr/games"), KernelFsBlockDeviceFormatCustomMiniFs, KernelUsrGamesSize, &kernelUsrGamesReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/usr/man/1"), KernelFsBlockDeviceFormatCustomMiniFs, KernelMan1Size, &kernelMan1ReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/usr/man/2"), KernelFsBlockDeviceFormatCustomMiniFs, KernelMan2Size, &kernelMan2ReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/usr/man/3"), KernelFsBlockDeviceFormatCustomMiniFs, KernelMan3Size, &kernelMan3ReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/curses"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibCursesSize, &kernelLibCursesReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/pin"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibPinSize, &kernelLibPinReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/sys"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibSysSize, &kernelLibSysReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/std/io"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdIoSize, &kernelLibStdIoReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/std/math"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdMathSize, &kernelLibStdMathReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/std/proc"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdProcSize, &kernelLibStdProcReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/std/mem"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdMemSize, &kernelLibStdMemReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/std/str"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdStrSize, &kernelLibStdStrReadFunctor, NULL, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/lib/std/time"), KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdTimeSize, &kernelLibStdTimeReadFunctor, NULL, NULL);
	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /lib and /usr\n"));

	// ... optional EEPROM volumes
	error=false;
	error|=!kernelFsAddBlockDeviceFile(kstrP("/etc"), KernelFsBlockDeviceFormatCustomMiniFs, KernelEepromEtcSize, &kernelEtcReadFunctor, kernelEtcWriteFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile(kstrP("/dev/eeprom"), KernelFsBlockDeviceFormatFlatFile, KernelEepromDevEepromSize, &kernelDevEepromReadFunctor, kernelDevEepromWriteFunctor, NULL);
	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /etc and /home\n"));

	// ... optional device files
	error=false;
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/ttyS0"), &kernelDevTtyS0ReadFunctor, &kernelDevTtyS0CanReadFunctor, &kernelDevTtyS0WriteFunctor, true, NULL);

	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/zero"), &kernelDevZeroReadFunctor, &kernelDevZeroCanReadFunctor, &kernelDevZeroWriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/full"), &kernelDevFullReadFunctor, &kernelDevFullCanReadFunctor, &kernelDevFullWriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/null"), &kernelDevNullReadFunctor, &kernelDevNullCanReadFunctor, &kernelDevNullWriteFunctor, true, NULL);
	error|=!kernelFsAddCharacterDeviceFile(kstrP("/dev/urandom"), &kernelDevURandomReadFunctor, &kernelDevURandomCanReadFunctor, &kernelDevURandomWriteFunctor, true, NULL);

	if (error)
		kernelLog(LogTypeWarning, kstrP("fs init failure: /dev\n"));

	// ... optional pin device files
	// TODO: include all once we figure out how to fit into ram
	// For now just include digital pins 2 through 13 (avoiding serial pins 0 & 1, and including led pin at 13)
	uint8_t pinsAdded=0;
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin36"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD2);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin37"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD3);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin53"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD4);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin35"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD5);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin59"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD6);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin60"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD7);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin61"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD8);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin62"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD9);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin12"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD10);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin13"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD11);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin14"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD12);
	pinsAdded+=kernelFsAddCharacterDeviceFile(kstrP("/dev/pin15"), &kernelDevPinReadFunctor, &kernelDevPinCanReadFunctor, &kernelDevPinWriteFunctor, false, (void *)(uintptr_t)PinD13);

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

int16_t kernelBinReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelBinSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmembinData)+addr);
	#else
	return progmembinData[addr];
	#endif
}

int16_t kernelLibCursesReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibCursesSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibcursesData)+addr);
	#else
	return progmemlibcursesData[addr];
	#endif
}

int16_t kernelLibPinReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibPinSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibpinData)+addr);
	#else
	return progmemlibpinData[addr];
	#endif
}

int16_t kernelLibSysReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibSysSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibsysData)+addr);
	#else
	return progmemlibsysData[addr];
	#endif
}

int16_t kernelLibStdIoReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdIoSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibstdioData)+addr);
	#else
	return progmemlibstdioData[addr];
	#endif
}

int16_t kernelLibStdMathReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdMathSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibstdmathData)+addr);
	#else
	return progmemlibstdmathData[addr];
	#endif
}

int16_t kernelLibStdProcReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdProcSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibstdprocData)+addr);
	#else
	return progmemlibstdprocData[addr];
	#endif
}

int16_t kernelLibStdMemReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdProcSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibstdmemData)+addr);
	#else
	return progmemlibstdmemData[addr];
	#endif
}

int16_t kernelLibStdStrReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdStrSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibstdstrData)+addr);
	#else
	return progmemlibstdstrData[addr];
	#endif
}

int16_t kernelLibStdTimeReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelLibStdTimeSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemlibstdtimeData)+addr);
	#else
	return progmemlibstdtimeData[addr];
	#endif
}

int16_t kernelMan1ReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelMan1Size);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemman1Data)+addr);
	#else
	return progmemman1Data[addr];
	#endif
}

int16_t kernelMan2ReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelMan2Size);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemman2Data)+addr);
	#else
	return progmemman2Data[addr];
	#endif
}

int16_t kernelMan3ReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelMan3Size);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemman3Data)+addr);
	#else
	return progmemman3Data[addr];
	#endif
}

int16_t kernelDevEepromReadFunctor(KernelFsFileOffset addr, void *userData) {
	addr+=KernelEepromDevEepromOffset;
#ifdef ARDUINO
	return eeprom_read_byte((void *)addr);
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %u in /dev/eeprom read functor\n"), addr);
		return -1;
	}
	int c=fgetc(kernelFakeEepromFile);
	if (c==EOF) {
		kernelLog(LogTypeWarning, kstrP("could not read at addr %u in /dev/eeprom read functor\n"), addr);
		return -1;
	}
	return c;
#endif
}

bool kernelDevEepromWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	addr+=KernelEepromDevEepromOffset;
#ifdef ARDUINO
	eeprom_update_byte((void *)addr, value);
	return true;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %u in /dev/eeprom write functor\n"), addr);
		return false;
	}
	if (fputc(value, kernelFakeEepromFile)==EOF) {
		kernelLog(LogTypeWarning, kstrP("could not write to addr %u in /dev/eeprom write functor\n"), addr);
		return false;
	}

	return true;
#endif
}

int16_t kernelEtcReadFunctor(KernelFsFileOffset addr, void *userData) {
	addr+=KernelEepromEtcOffset;
#ifdef ARDUINO
	return eeprom_read_byte((void *)addr);
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %u in etc read functor\n"), addr);
		return -1;
	}
	int c=fgetc(kernelFakeEepromFile);
	if (c==EOF) {
		kernelLog(LogTypeWarning, kstrP("could not read at addr %u in etc read functor\n"), addr);
		return -1;
	}
	return c;
#endif
}

bool kernelEtcWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	addr+=KernelEepromEtcOffset;
#ifdef ARDUINO
	eeprom_update_byte((void *)addr, value);
	return true;
#else
	if (fseek(kernelFakeEepromFile, addr, SEEK_SET)!=0 || ftell(kernelFakeEepromFile)!=addr) {
		kernelLog(LogTypeWarning, kstrP("could not seek to addr %u in etc write functor\n"), addr);
		return false;
	}
	if (fputc(value, kernelFakeEepromFile)==EOF) {
		kernelLog(LogTypeWarning, kstrP("could not write to addr %u in etc write functor\n"), addr);
		return false;
	}

	return true;
#endif
}

uint8_t kernelEtcMiniFsReadFunctor(uint16_t addr, void *userData) {
	int16_t value=kernelEtcReadFunctor(addr, userData);
	assert(value>=0 && value<256);
	return value;
}

void kernelEtcMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelEtcWriteFunctor(addr, value, userData);
	assert(result);
	_unused(result);
}

int16_t kernelTmpReadFunctor(KernelFsFileOffset addr, void *userData) {
	return kernelTmpDataPool[addr];
}

bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value, void *userData) {
	kernelTmpDataPool[addr]=value;
	return true;
}

uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData) {
	int16_t value=kernelTmpReadFunctor(addr, userData);
	assert(value>=0 && value<256);
	return value;
}

void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelTmpWriteFunctor(addr, value, userData);
	assert(result);
	_unused(result);
}

int16_t kernelDevZeroReadFunctor(void *userData) {
	return 0;
}

bool kernelDevZeroCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevZeroWriteFunctor(uint8_t value, void *userData) {
	return true;
}

int16_t kernelDevFullReadFunctor(void *userData) {
	return 0;
}

bool kernelDevFullCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevFullWriteFunctor(uint8_t value, void *userData) {
	return false;
}

int16_t kernelDevNullReadFunctor(void *userData) {
	return 0;
}

bool kernelDevNullCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevNullWriteFunctor(uint8_t value, void *userData) {
	return true;
}

int16_t kernelDevURandomReadFunctor(void *userData) {
	return rand()&0xFF;
}

bool kernelDevURandomCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevURandomWriteFunctor(uint8_t value, void *userData) {
	return false;
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

bool kernelDevTtyS0WriteFunctor(uint8_t value, void *userData) {
#ifdef ARDUINO
	if (putchar(value)!=value)
		return false;
	fflush(stdout);
	return true;
#else
	if (putchar(value)!=value)
		return false;
	fflush(stdout);
	return true;
#endif
}

int16_t kernelUsrBinReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelUsrBinSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemusrbinData)+addr);
	#else
	return progmemusrbinData[addr];
	#endif
}

int16_t kernelUsrGamesReadFunctor(KernelFsFileOffset addr, void *userData) {
	assert(addr<KernelUsrGamesSize);
	#ifdef ARDUINO
	return pgm_read_byte_far(pgm_get_far_address(progmemusrgamesData)+addr);
	#else
	return progmemusrgamesData[addr];
	#endif
}

int16_t kernelDevPinReadFunctor(void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;
	return pinRead(pinNum);
}

bool kernelDevPinCanReadFunctor(void *userData) {
	return true;
}

bool kernelDevPinWriteFunctor(uint8_t value, void *userData) {
	uint8_t pinNum=(uint8_t)(uintptr_t)userData;
	return pinWrite(pinNum, (value!=0));
}

#ifndef ARDUINO
void kernelSigIntHandler(int sig) {
	for(uint8_t i=0; i<kernelReaderPidArrayMax; ++i) {
		if (kernelReaderPidArray[i]!=ProcManPidMax)
			procManProcessSendSignal(kernelReaderPidArray[i], ByteCodeSignalIdInterrupt);
	}
}
#endif
