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

			if (byte=='u') {
				if (padZero)
					sprintf(vfprintf_PFFormatStr, "%%0%uu", padLen);
				else
					sprintf(vfprintf_PFFormatStr, "%%%uu", padLen);
				uint16_t val=va_arg(ap, uint16_t);
				int16_t subRes=fprintf(stream, vfprintf_PFFormatStr, val);
				if (subRes<0)
					break;
				written+=subRes;
			} else if (byte=='i') {
				if (padZero)
					sprintf(vfprintf_PFFormatStr, "%%0%ui", padLen);
				else
					sprintf(vfprintf_PFFormatStr, "%%%ui", padLen);
				uint16_t val=va_arg(ap, uint16_t);
				int16_t subRes=fprintf(stream, vfprintf_PFFormatStr, val);
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

#endif