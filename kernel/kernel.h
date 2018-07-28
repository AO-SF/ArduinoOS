#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

#ifndef ARDUINO
uint32_t millisRaw(void); // time in ms since unix epoch
uint32_t millis(void); // time in ms since boot
#endif

#endif
