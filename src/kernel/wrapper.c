// TODO: arduino stuff here is a horrible hack
// avr-libc requires init, maintaining and only gives 1s res, need something better

#ifdef ARDUINO
//#include <time.h>
#include <stdint.h>
uint32_t tempTime=0;
#include <util/delay.h>
#else
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "wrapper.h"

uint32_t kernelBootTime=0;

uint32_t millisRaw(void) {
	#ifdef ARDUINO
	//return time(NULL)*((uint32_t)1000);
	return 100*(tempTime++);
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
	_delay_ms(5);
	// ..... _delay_ms(ms);
	#else
	usleep(ms*1000llu);
	#endif
}

