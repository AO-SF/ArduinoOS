#ifdef ARDUINO
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <util/delay.h>
char vfprintf_PFFormatStr[8];
#else
#include <stdlib.h>
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

#ifdef ARDUINO
int16_t fprintf_PF(FILE *stream, uint_farptr_t format, ...) {
	va_list ap;
	va_start(ap, format);
	int16_t res=vfprintf_PF(stream, format, ap);
	va_end(ap);
	return res;
}


int16_t vfprintf_PF(FILE *stream, uint_farptr_t format, va_list ap) {
	int16_t written=0;
	uint8_t byte;
	for(; (byte=pgm_read_byte_far(format))!='\0'; ++format) {
		if (byte=='\\') {
			// TODO: Support more, e.g. \t
			byte=pgm_read_byte_far(++format);
			if (byte=='\0')
				break;
			if (byte=='n')
				written+=(fputc('\n', stream)!=EOF);
		} else if (byte=='%') {
			// TODO: Support more than single-digit length strings
			byte=pgm_read_byte_far(++format);
			if (byte=='\0')
				break;
			bool padZero=false;
			if (byte=='0') {
				padZero=true;
				byte=pgm_read_byte_far(++format);
				if (byte=='\0')
					break;
			}
			uint8_t padLen=0;
			if (byte>='0' && byte<='9') {
				padLen=byte-'0';
				byte=pgm_read_byte_far(++format);
				if (byte=='\0')
					break;
			}

			if (byte=='u') {
				if (padZero)
					sprintf(vfprintf_PFFormatStr, "%%0%uu", padLen);
				else
					sprintf(vfprintf_PFFormatStr, "%%%uu", padLen);
				uint16_t val=va_arg(ap, uint16_t);
				int16_t subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='i') {
				if (padZero)
					sprintf(vfprintf_PFFormatStr, "%%0%ui", padLen);
				else
					sprintf(vfprintf_PFFormatStr, "%%%ui", padLen);
				uint16_t val=va_arg(ap, uint16_t);
				int16_t subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='s') {
				const char *str=va_arg(ap, const char *);
				int16_t subRes=fprintf(stream, "%s", str);
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='p') {
				void *ptr=va_arg(ap, void *);
				int16_t subRes=fprintf(stream, "%p", ptr);
				if (subRes<0)
					break;
				written+=subRes;
			}
		} else
			written+=(fputc(byte, stream)!=EOF);
	}
	return written;
}
#endif
