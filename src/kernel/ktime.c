#include <inttypes.h>
#include <stdlib.h>
#ifdef ARDUINO
#include <avr/interrupt.h>
#include <avr/io.h>
#else
#include <sys/time.h>
#endif

#include "log.h"
#include "ktime.h"
#include "util.h"

KTime ktimeBootTime=0;
KTime ktimeRealTimeOffset=0; // offset to add to monotonic time to get real time

KTime ktimeGetRawMs(void); // like ktimeGetMonotonicMs() but offset by some constant (constant is arbitrary in arduino build, but in pc version it is such that this function returns the same as ktimeGetRealMs())

#ifdef ARDUINO
#define clockCyclesPerMicrosecond (F_CPU/1000000L)
#define clockCyclesToMicroseconds(a) ((a)/clockCyclesPerMicrosecond)

// Set prescaler so timer0 ticks every 64 clock cycles, and the overflow handler every 256 ticks.
#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64*256))
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW/1000)
#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW%1000)>>3)
#define FRACT_MAX (1000>>3)

volatile KTime millisTimerOverflowCount=0;
volatile KTime millisTimerValue=0;
static uint8_t millisTimerFractional=0;

ISR(TIMER0_OVF_vect) {
	// Read volatile values
	KTime m=millisTimerValue;
	uint8_t f=millisTimerFractional;

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
	// Arduino only: set timer 0 prescale factor to 64 and enable overflow interrupt
	#ifdef ARDUINO
	TCCR0B|=(1u<<CS01);
	TCCR0B|=(1u<<CS00);
	TIMSK0|=(1u<<TOIE0);
	#endif

	// Set boot time
	ktimeBootTime=ktimeGetRawMs();
	kernelLog(LogTypeInfo, kstrP("set kernel boot time to %"PRIu64"\n"), ktimeBootTime);

	// In PC wrapper we can update real time offset at any time, so might as well do it now.
	#ifndef ARDUINO
	ktimeSetRealMs(ktimeGetRawMs());
	#endif
}

KTime ktimeGetMonotonicMs(void) {
	return ktimeGetRawMs()-ktimeBootTime;
}

KTime ktimeGetRealMs(void) {
	return ktimeGetMonotonicMs()+ktimeRealTimeOffset;
}

void ktimeDelayMs(KTime ms) {
	#ifdef ARDUINO
	_delay_ms(ms);
	#else
	usleep(ms*1000llu);
	#endif
}

void ktimeSetRealMs(KTime ms) {
	// Update real time offset such that ktimeGetRealMs would return ms if called at this moment
	KTime newOffset=ms-ktimeGetMonotonicMs();
	if (newOffset==ktimeRealTimeOffset)
		return;

	kernelLog(LogTypeInfo, kstrP("set kernel real time offset to %"PRIu64" (was %"PRIu64")\n"), newOffset, ktimeRealTimeOffset);
	ktimeRealTimeOffset=newOffset;
}

KTime ktimeGetRawMs(void) {
	#ifdef ARDUINO
	KTime ms;
	uint8_t oldSReg=SREG;
	cli();
	ms=millisTimerValue;
	SREG=oldSReg;
	return ms;
	#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec*1000llu+tv.tv_usec/1000llu;
	#endif
}

void ktimeTimeMsToDate(KTime input, KDate *date) {
	date->ms=input%1000;
	date->second=(input/1000)%60;
	date->minute=(input/1000)/60%60;
	date->hour=(input/1000)/60/60%24;

	unsigned z=(input/1000)/86400; // days since 1st jan 1970
	z+=719468;
	unsigned era=z/146097;
	unsigned doe=(z-era*146097); // [0, 146096]
	unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365; // [0, 399]
	unsigned y=yoe+era*400;
	unsigned doy=doe-(365*yoe+yoe/4-yoe/100); // [0, 365]
	unsigned mp=(5*doy+2)/153; // [0, 11]
	unsigned d=doy-(153*mp+2)/5+1; // [1, 31]
	unsigned m=mp+(mp < 10 ? 3 : -9); // [1, 12]
	y+=(m<=2);

	date->year=y;
	date->month=m;
	date->day=d;
}
