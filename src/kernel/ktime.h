#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdint.h>

void ktimeInit(void);

uint32_t ktimeGetMs(void); // ms since boot

void ktimeDelayMs(uint32_t ms);

#endif
