#include <assert.h>
#include <string.h>

#ifdef ARDUINO
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <util/atomic.h>
#else
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "circbuf.h"
#include "kernelfs.h"
#include "kstr.h"
#include "log.h"
#include "procman.h"
#include "tty.h"

#ifdef ARDUINO
#include "uart.h"
#endif

typedef enum {
	TtyControlCharacterSetNone=0,
	TtyControlCharacterSetBreak=1, // ctrl+c
} TtyControlCharacterSet;
volatile uint8_t ttyControlCharacterSet=TtyControlCharacterSetNone;

volatile bool ttyEchoFlag;
volatile bool ttyBlockingFlag;
volatile CircBuf ttyCircBuf;
volatile uint8_t ttyCircBufNewlineCount;

#ifndef ARDUINO
static struct termios ttyOldConfig;
#endif

#ifndef ARDUINO
void ttySigIntHandler(int sig);
#endif

void ttyControlActivate(TtyControlCharacterSet control); // should be called on detection often such a control key

bool ttyHandleByte(uint8_t value);

#ifdef ARDUINO
ISR(USART0_RX_vect) {
	ATOMIC_BLOCK(ATOMIC_FORCEON) {
		ttyHandleByte(UDR0);
	}
}
#endif

void ttyInit(void) {
	// Initialise common fields
	ttyEchoFlag=true;
	ttyBlockingFlag=true;
	ttyCircBufNewlineCount=0;
	circBufInit(&ttyCircBuf);

	// Arduino only: init uart for serial (for kernel logging, and ready to map to /dev/ttyS0).
#ifdef ARDUINO
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

	// PC only - register sigint handler so we can pass this signal onto e.g. the shell
#ifndef ARDUINO
    signal(SIGINT, ttySigIntHandler);
#endif

	// PC only: put terminal 'raw' mode so we can handle things such as ctrl+d ourselves
#ifndef ARDUINO
	static struct termios newConfig;
	tcgetattr(STDIN_FILENO, &ttyOldConfig);

	newConfig=ttyOldConfig;
	newConfig.c_lflag&=~(ICANON|ECHO);

	tcsetattr(STDIN_FILENO, TCSANOW, &newConfig);
#endif
}

void ttyQuit(void) {
	// Non-arduino-only: reset terminal settings
#ifndef ARDUINO
	tcsetattr(STDIN_FILENO, TCSANOW, &ttyOldConfig);
#endif
}

void ttyTick(void) {
	// PC only: check if any data has arrived
#ifndef ARDUINO
	// Poll for input events on stdin
	struct pollfd pollFds[0];
	memset(pollFds, 0, sizeof(pollFds));
	pollFds[0].fd=STDIN_FILENO;
	pollFds[0].events=POLLIN;
	if (poll(pollFds, 1, 0)>0 && (pollFds[0].revents & POLLIN)) {
		// Call ioctl to find number of bytes available
		int available;
		ioctl(STDIN_FILENO, FIONREAD, &available);

		// Read as many bytes as we can
		while(available>0) {
			int value=getchar();
			if (value==EOF)
				break;

			if (!ttyHandleByte(value))
				break;

			--available;
		}
	}
#endif

	// Check for break (ctrl+c)
	if (ttyControlCharacterSet & TtyControlCharacterSetBreak) {
		// Write to lo
		kernelLog(LogTypeInfo, kstrP("ctrl+c flagged, sending interrupt to processes with '/dev/ttyS0' open\n"));

		// Loop over all processes looking for those with /dev/ttyS0 open
		for(ProcManPid pid=0; pid<ProcManPidMax; ++pid) {
			// Get table of open fds (simply fails if process doesn't exist, no need to check first)
			KernelFsFd fds[ProcManMaxFds];
			if (!procManProcessGetOpenFds(pid, fds))
				continue;

			// Look through table for an fd representing '/dev/ttyS0'
			for(unsigned i=0; i<ProcManMaxFds; ++i)
				if (fds[i]!=KernelFsFdInvalid && kstrDoubleStrcmp(kstrP("/dev/ttyS0"), kernelFsGetFilePath(fds[i]))==0) {
					// Send interrupt to this process
					procManProcessSendSignal(pid, BytecodeSignalIdInterrupt);

					// Only send once per program, so break
					break;
				}
		}

		// Clear flag to be ready for next time
		ttyControlCharacterSet&=~TtyControlCharacterSetBreak;
	}
}

int16_t ttyReadFunctor(void) {
	int16_t ret=-1;

	if (ttyCanReadFunctor()) {
		uint8_t value;
#ifdef ARDUINO
		ATOMIC_BLOCK(ATOMIC_FORCEON) {
#else
		if (1) {
#endif
			if (circBufPop(&ttyCircBuf, &value)) {
				ret=value;
				if (value=='\n')
					--ttyCircBufNewlineCount;
			}
		}
	}

	return ret;
}

bool ttyCanReadFunctor(void) {
	if (ttyCircBufNewlineCount>0)
		return true;
	if (ttyBlockingFlag)
		return false;
	return !circBufIsEmpty(&ttyCircBuf);
}

KernelFsFileOffset ttyWriteFunctor(const uint8_t *data, KernelFsFileOffset len) {
	if (len>UINT16_MAX)
		len=UINT16_MAX;

	KernelFsFileOffset written=fwrite(data, 1, len, stdout);
	fflush(stdout);

	// FIXME: KernelFsFileOffset is unsigned so check below is a hack.
	if (written>=UINT32_MAX-256) // should be written<0
		written=0;

	return written;
}

bool ttyCanWriteFunctor(void) {
	return true;
}

bool ttyGetBlocking(void) {
	return ttyBlockingFlag;
}

bool ttyGetEcho(void) {
	return ttyEchoFlag;
}

void ttySetBlocking(bool blocking) {
	ttyBlockingFlag=blocking;
}

void ttySetEcho(bool echo) {
	ttyEchoFlag=echo;
}

#ifndef ARDUINO
void ttySigIntHandler(int sig) {
	ttyControlActivate(TtyControlCharacterSetBreak);
}
#endif

void ttyControlActivate(TtyControlCharacterSet control) {
	ttyControlCharacterSet|=control;
}

bool ttyHandleByte(uint8_t value) {
	switch(value) {
		case 3:
			// Ctrl+c
			ttyControlActivate(TtyControlCharacterSetBreak);
		break;
		case 127: {
			// Backspace - try to remove last char from buffer, unless it is a newline
			uint8_t tailValue;
			if (circBufTailPeek(&ttyCircBuf, &tailValue)) {
				if (tailValue!='\n' && circBufUnpush(&ttyCircBuf)) {
					// Unpush call remove lasts character from buffer - check if we also need to update the display.
					if (ttyEchoFlag) {
						// Clear last char on screen
						const uint8_t tempChars[3]={8,' ',8};
						ttyWriteFunctor(tempChars, 3);
					}
				}
			}
		} break;
		default: {
			// Standard character

			// Special case: convert carriage returns to newlines
			if (value=='\r')
				value='\n';

			//  Add byte to buffer and update state
			if (!circBufPush(&ttyCircBuf, value))
				return false;

			if (value=='\n')
				++ttyCircBufNewlineCount;

			// If required, also update display.
			if (ttyEchoFlag)
				ttyWriteFunctor(&value, 1);
		} break;
	}

	return true;
}
