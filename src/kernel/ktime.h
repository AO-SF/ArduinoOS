#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>
#ifdef ARDUINO
#include <util/delay.h>
#else
#include <unistd.h>
#endif

void ktimeInit(void);

uint32_t ktimeGetMs(void); // ms since boot

void ktimeDelayMs(uint32_t ms);

// Note: we have to define ktimeDelayUs this way as the AVR libc function _delay_us needs a compile time constant argument
#ifdef ARDUINO
#define ktimeDelayUs(us) _delay_us(us)
#else
#define ktimeDelayUs(us) usleep(us)
#endif

#endif
