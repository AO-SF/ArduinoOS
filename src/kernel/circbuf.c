#include <assert.h>
#include <stdlib.h>

#include "circbuf.h"

#define circBufIndexNext(i) (((i)==cb->size-1) ? 0 : ((i)+1))
#define circBufIndexPrev(i) (((i)==0) ? cb->size-1 : ((i)-1))

void circBufInit(volatile CircBuf *cb, volatile uint8_t *buffer, uint8_t size) {
	assert(cb!=NULL);

	cb->buffer=buffer;
	cb->size=size;
	cb->head=cb->tail=0;
}

bool circBufIsEmpty(volatile CircBuf *cb) {
	return cb->head==cb->tail;
}

bool circBufPush(volatile CircBuf *cb, uint8_t value) {
	assert(cb!=NULL);

	uint8_t newTail=circBufIndexNext(cb->tail);

	// full?
	if (newTail==cb->head)
		return false;

	// write value
	cb->buffer[cb->tail]=value;

	// push value
	cb->tail=newTail;

	return true;
}

bool circBufPop(volatile CircBuf *cb, uint8_t *value) {
	assert(cb!=NULL);
	assert(value!=NULL);

	// empty?
	if (cb->head==cb->tail)
		return false;

	// read value
	*value=cb->buffer[cb->head];

	// pop value
	cb->head=circBufIndexNext(cb->head);

	return true;
}

bool circBufUnpush(volatile CircBuf *cb) {
	assert(cb!=NULL);

	// empty?
	if (cb->head==cb->tail)
		return false;

	// pop from tail
	cb->tail=circBufIndexPrev(cb->tail);

	return true;
}

bool circBufTailPeek(volatile CircBuf *cb, uint8_t *value) {
	assert(cb!=NULL);

	// empty?
	if (cb->head==cb->tail)
		return false;

	// peek at tail
	*value=cb->buffer[circBufIndexPrev(cb->tail)];

	return true;
}
