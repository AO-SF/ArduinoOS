#ifndef TTY_H
#define TTY_H

#include <stdint.h>
#include <stdbool.h>

void ttyInit(void);
void ttyQuit(void);

void ttyTick(void);

int16_t ttyReadFunctor(void);
bool ttyCanReadFunctor(void);
KernelFsFileOffset ttyWriteFunctor(const uint8_t *data, KernelFsFileOffset len);
bool ttyCanWriteFunctor(void);

bool ttyGetBlocking(void); // If true (which is the default) then waits for a newline before bytes are available in read functor, otherwise they are available immediately
bool ttyGetEcho(void); // If true (which is the default) then echos (writes) any characters read.

void ttySetBlocking(bool blocking);
void ttySetEcho(bool echo);


#endif