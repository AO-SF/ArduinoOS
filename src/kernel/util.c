#include "util.h"

bool isPow2(unsigned x) {
	return x && !(x & (x-1));
}
