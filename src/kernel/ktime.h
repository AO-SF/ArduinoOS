#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>
#ifdef ARDUINO
#include <util/delay.h>
#else
#include <unistd.h>
#endif

typedef uint64_t KTime;

typedef struct {
	uint32_t year; // 1970-~584,526,042
	uint8_t month; // 1-12
	uint8_t day; // 1-31
	uint8_t hour; // 0-23
	uint8_t minute; // 0-59
	uint8_t second; // 0-59
	uint16_t ms; // 0-999
} KDate;

void ktimeInit(void);

KTime ktimeGetMonotonicMs(void); // ms since boot
KTime ktimeGetRealMs(void); // ms since 1st Jan 1970

void ktimeDelayMs(KTime ms);

void ktimeSetRealMs(KTime ms); // if we get an update of the current real time then pass it to this function to sync

// Note: we have to define ktimeDelayUs this way as the AVR libc function _delay_us needs a compile time constant argument
#ifdef ARDUINO
#define ktimeDelayUs(us) _delay_us(us)
#else
#define ktimeDelayUs(us) usleep(us)
#endif

void ktimeTimeMsToDate(KTime input, KDate *date);

#endif
