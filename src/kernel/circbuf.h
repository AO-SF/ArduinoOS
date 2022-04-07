#ifndef CIRCBUF_H
#define CIRCBUF_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	volatile uint8_t *buffer;
	uint8_t size;
	volatile uint8_t head, tail; // push to tail, pop from head
} CircBuf;

void circBufInit(volatile CircBuf *cb, volatile uint8_t *buffer, uint8_t size);

bool circBufIsEmpty(volatile CircBuf *cb);

bool circBufPush(volatile CircBuf *cb, uint8_t value);
bool circBufPop(volatile CircBuf *cb, uint8_t *value);

bool circBufUnpush(volatile CircBuf *cb); // to implement backspace

bool circBufTailPeek(volatile CircBuf *cb, uint8_t *value);

#endif
