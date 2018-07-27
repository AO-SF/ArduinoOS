#ifndef BINPROGMEM_H
#define BINPROGMEM_H

#include <stdint.h>

// TODO: this needs to be done specially for arduino
#define BINPROGMEMDATASIZE (6*1024u)
extern const uint8_t binProgmemData[BINPROGMEMDATASIZE];

#endif
