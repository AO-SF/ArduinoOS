#ifndef KERNEL_H
#define KERNEL_H

#include "procman.h"

typedef enum {
	KernelStateInvalid,
	KernelStateBooting,
	KernelStateRunning,
	KernelStateShuttingDownWaitAll,
	KernelStateShuttingDownWaitInit,
	KernelStateShuttingDownFinal,
	KernelStateShutdown,
} KernelState;

#define kernelTickMinTimeMs 10

#ifndef ARDUINO
extern ProcManPid kernelReaderPid; // set to whoever has /dev/ttyS0 open, used for ctrl+c propagation from host
#endif

void kernelShutdownBegin(void);

KernelState kernelGetState(void);

#endif
