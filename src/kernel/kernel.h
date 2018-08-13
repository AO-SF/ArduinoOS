#ifndef KERNEL_H
#define KERNEL_H

#include "procman.h"

#ifndef ARDUINO
extern ProcManPid kernelReaderPid; // set to whoever has /dev/ttyS0 open, used for ctrl+c propagation from host
#endif

#endif
