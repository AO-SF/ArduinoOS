#ifndef ARDUINO

#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "wrapper.h"

uint32_t kernelBootTime=0;

uint32_t millisRaw(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL); // TODO: Check return value.
	return tv.tv_sec*1000llu+tv.tv_usec/1000llu;
}

uint32_t millis(void) {
	return millisRaw()-kernelBootTime;
}

void delay(uint32_t ms) {
	usleep(ms*1000llu);
}

#endif
