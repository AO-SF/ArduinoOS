#ifndef CIRCBUF_H
#define CIRCBUF_H

#include <stdbool.h>
#include <stdint.h>

#define CircBufSize 256

typedef struct {
	volatile uint8_t buffer[CircBufSize];
	volatile uint8_t head, tail; // push to tail, pop from head
} CircBuf;

void circBufInit(volatile CircBuf *cb);

bool circBufIsEmpty(volatile CircBuf *cb);

bool circBufPush(volatile CircBuf *cb, uint8_t value);
bool circBufPop(volatile CircBuf *cb, uint8_t *value);

#endif
