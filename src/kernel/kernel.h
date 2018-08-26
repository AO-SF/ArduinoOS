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

#ifdef ARDUINO
extern volatile bool kernelDevTtyS0EchoFlag;
extern volatile bool kernelDevTtyS0BlockingFlag;
#else
#endif

extern ProcManPid kernelReaderPid; // set to whoever has /dev/ttyS0 open, used for ctrl+c propagation from host

void kernelShutdownBegin(void);

KernelState kernelGetState(void);

#endif
