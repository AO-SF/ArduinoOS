#include <assert.h>
#include <stdlib.h>

#include "circbuf.h"

void circBufInit(volatile CircBuf *cb) {
	assert(cb!=NULL);

	cb->head=cb->tail=0;
}

bool circBufIsEmpty(volatile CircBuf *cb) {
	return cb->head==cb->tail;
}

bool circBufPush(volatile CircBuf *cb, uint8_t value) {
	assert(cb!=NULL);

	uint8_t newTail=(cb->tail==255 ? 0 : cb->tail+1);

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
	if (cb->head==255)
		cb->head=0;
	else
		cb->head++;

	return true;
}

bool circBufUnpush(volatile CircBuf *cb) {
	assert(cb!=NULL);

	// empty?
	if (cb->head==cb->tail)
		return false;

	// pop from tail
	cb->tail=(cb->tail==0 ? 255 :cb->tail-1);

	return true;
}

bool circBufTailPeek(volatile CircBuf *cb, uint8_t *value) {
	assert(cb!=NULL);

	// empty?
	if (cb->head==cb->tail)
		return false;

	// peek at tail
	*value=cb->buffer[cb->tail==0 ? 255 : cb->tail-1];

	return true;
}
