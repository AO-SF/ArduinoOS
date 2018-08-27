#include <stdlib.h>
#ifdef ARDUINO
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#include "log.h"
#include "wrapper.h"

uint32_t kernelBootTime=0;

#ifdef ARDUINO
#define clockCyclesPerMicrosecond (F_CPU/1000000L)
#define clockCyclesToMicroseconds(a) ((a)/clockCyclesPerMicrosecond)

// Set prescaler so timer0 ticks every 64 clock cycles, and the overflow handler every 256 ticks.
#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64*256))
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW/1000)
#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW%1000)>>3)
#define FRACT_MAX (1000>>3)

volatile unsigned long millisTimerOverflowCount=0;
volatile unsigned long millisTimerValue=0;
static unsigned char millisTimerFractional=0;

ISR(TIMER0_OVF_vect) {
	// Read volatile values
	unsigned long m=millisTimerValue;
	unsigned char f=millisTimerFractional;

	m+=MILLIS_INC;
	f+=FRACT_INC;
	if (f>=FRACT_MAX) {
		f-=FRACT_MAX;
		++m;
	}

	millisTimerFractional=f;
	millisTimerValue=m;
	millisTimerOverflowCount++;
}

#endif

void millisInit(void) {
#ifdef ARDUINO
	// Set timer 0 prescale factor to 64 and enable overflow interrupt
	TCCR0B|=(1u<<CS01);
	TCCR0B|=(1u<<CS00);
	TIMSK0|=(1u<<TOIE0);
#else
	// On the Arduino we can leave this at 0 but otherwise we have to save offset
	kernelBootTime=millisRaw();
	kernelLog(LogTypeInfo, kstrP("set kernel boot time to %lu (PC wrapper)\n"), kernelBootTime);
#endif
}

uint32_t millisRaw(void) {
	#ifdef ARDUINO
	uint32_t ms;
	uint8_t oldSReg=SREG;
	cli();
	ms=millisTimerValue;
	SREG=oldSReg;
	return ms;
	#else
	struct timeval tv;
	gettimeofday(&tv, NULL); // TODO: Check return value.
	return tv.tv_sec*1000llu+tv.tv_usec/1000llu;
	#endif
}

uint32_t millis(void) {
	return millisRaw()-kernelBootTime;
}

void delay(uint32_t ms) {
	#ifdef ARDUINO
	_delay_ms(ms);
	#else
	usleep(ms*1000llu);
	#endif
}
