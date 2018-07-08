#include "util.h"

uint16_t utilNextPow2(uint16_t x) {
	x--;
	x|=(x>>1);
	x|=(x>>2);
	x|=(x>>4);
	x|=(x>>8);
	x|=(x>>16);
	x++;
	return x;
}
