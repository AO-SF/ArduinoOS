/*

.....

kernel time should be signed 64 bit int?
will make subtractions etc safer
but still have more than enough resolution

*/

#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>
#ifdef ARDUINO
#include <util/delay.h>
#else
#include <unistd.h>
#endif

void ktimeInit(void);

uint64_t ktimeGetMonotonicMs(void); // ms since boot
uint64_t ktimeGetRealMs(void); // ms since 1st Jan 1970

void ktimeDelayMs(uint64_t ms);

void ktimeSetRealMs(uint64_t ms); // if we get an update of the current real time then pass it to this function to sync

// Note: we have to define ktimeDelayUs this way as the AVR libc function _delay_us needs a compile time constant argument
#ifdef ARDUINO
#define ktimeDelayUs(us) _delay_us(us)
#else
#define ktimeDelayUs(us) usleep(us)
#endif

#endif
