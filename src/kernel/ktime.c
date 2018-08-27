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
#include "ktime.h"

uint32_t ktimeBootTime=0;

uint32_t ktimeGetMsRaw(void);

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

void ktimeInit(void) {
#ifdef ARDUINO
	// Set timer 0 prescale factor to 64 and enable overflow interrupt
	TCCR0B|=(1u<<CS01);
	TCCR0B|=(1u<<CS00);
	TIMSK0|=(1u<<TOIE0);
#endif

	ktimeBootTime=ktimeGetMsRaw();
	kernelLog(LogTypeInfo, kstrP("set kernel boot time to %lu\n"), ktimeBootTime);
}

uint32_t ktimeGetMs(void) {
	return ktimeGetMsRaw()-ktimeBootTime;
}

void ktimeDelayMs(uint32_t ms) {
	#ifdef ARDUINO
	_delay_ms(ms);
	#else
	usleep(ms*1000llu);
	#endif
}

uint32_t ktimeGetMsRaw(void) {
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
