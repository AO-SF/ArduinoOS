#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

#ifndef ARDUINO
#define PROGMEM
#endif

#define _unused(x) ((void)(x))

#define STATICASSERT4(COND,MSG) typedef char static_assertion_##MSG[(!!(COND))*2-1]
#define STATICASSERT3(X,L) STATICASSERT4(X,static_assertion_at_line_##L)
#define STATICASSERT2(X,L) STATICASSERT3(X,L)
#define STATICASSERT(X) STATICASSERT2(X,__LINE__)

#ifndef MIN
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif

#ifndef PRIu64
#ifdef ARDUINO
#define PRIu64 "llu"
#else
#define PRIu64 "lu"
#endif
#endif

int clz8(uint8_t x); // returns 8 if x=0
int clz16(uint16_t x); // returns 16 if x=0

#endif
