#ifdef ARDUINO

#include <avr/pgmspace.h>
#include <avr/io.h>
#include <stdbool.h>

#include "avrlib.h"

char vfprintf_PFFormatStr[8];

int16_t fprintf_PF(FILE *stream, uint_farptr_t format, ...) {
	va_list ap;
	va_start(ap, format);
	int16_t res=vfprintf_PF(stream, format, ap);
	va_end(ap);
	return res;
}

int16_t vfprintf_PF(FILE *stream, uint_farptr_t format, va_list ap) {
	int16_t written=0;
	uint8_t byte;
	for(; (byte=pgm_read_byte_far(format))!='\0'; ++format) {
		if (byte=='\\') {
			// TODO: Support more, e.g. \t
			byte=pgm_read_byte_far(++format);
			if (byte=='\0')
				break;
			if (byte=='n')
				written+=(fputc('\n', stream)!=EOF);
		} else if (byte=='%') {
			// TODO: Support more than single-digit length strings
			byte=pgm_read_byte_far(++format);
			if (byte=='\0')
				break;
			bool padZero=false;
			if (byte=='0') {
				padZero=true;
				byte=pgm_read_byte_far(++format);
				if (byte=='\0')
					break;
			}
			uint8_t padLen=0;
			if (byte>='0' && byte<='9') {
				padLen=byte-'0';
				byte=pgm_read_byte_far(++format);
				if (byte=='\0')
					break;
			}

			bool longFlag=false;
			if (byte=='l') {
				longFlag=true;
				byte=pgm_read_byte_far(++format);
				if (byte=='\0')
					break;
			}

			if (byte=='u') {
				int16_t subRes;
				if (longFlag) {
					if (padZero)
						sprintf(vfprintf_PFFormatStr, "%%0%ulu", padLen);
					else
						sprintf(vfprintf_PFFormatStr, "%%%ulu", padLen);
					uint32_t val=va_arg(ap, uint32_t);
					subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				} else {
					if (padZero)
						sprintf(vfprintf_PFFormatStr, "%%0%uu", padLen);
					else
						sprintf(vfprintf_PFFormatStr, "%%%uu", padLen);
					uint16_t val=va_arg(ap, uint16_t);
					subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				}
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='i') {
				int16_t subRes;
				if (longFlag) {
					if (padZero)
						sprintf(vfprintf_PFFormatStr, "%%0%uli", padLen);
					else
						sprintf(vfprintf_PFFormatStr, "%%%uli", padLen);
					uint32_t val=va_arg(ap, uint32_t);
					subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				} else {
					if (padZero)
						sprintf(vfprintf_PFFormatStr, "%%0%ui", padLen);
					else
						sprintf(vfprintf_PFFormatStr, "%%%ui", padLen);
					uint16_t val=va_arg(ap, uint16_t);
					subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				}
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='X') {
				int16_t subRes;
				if (longFlag) {
					if (padZero)
						sprintf(vfprintf_PFFormatStr, "%%0%ulX", padLen);
					else
						sprintf(vfprintf_PFFormatStr, "%%%ulX", padLen);
					uint32_t val=va_arg(ap, uint32_t);
					subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				} else {
					if (padZero)
						sprintf(vfprintf_PFFormatStr, "%%0%uX", padLen);
					else
						sprintf(vfprintf_PFFormatStr, "%%%uX", padLen);
					uint16_t val=va_arg(ap, uint16_t);
					subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				}
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='s') {
				const char *str=va_arg(ap, const char *);
				int16_t subRes=fprintf(stream, "%s", str);
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='p') {
				void *ptr=va_arg(ap, void *);
				int16_t subRes=fprintf(stream, "%p", ptr);
				if (subRes<0)
					break;
				written+=subRes;
			}
		} else
			written+=(fputc(byte, stream)!=EOF);
	}
	return written;
}

int freeRam(void) {
	// taken from https://jeelabs.org/2011/05/22/atmega-memory-use/
	// modified to make avr-gcc happy
	extern int __heap_start, *__brkval;
	int v;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-local-addr"
	return (int)&v - (__brkval==0 ? (int)&__heap_start : (int)__brkval);
#pragma GCC diagnostic pop
}

#endif
