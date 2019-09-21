#ifndef UTIL_H
#define UTIL_H

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

#endif
