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

void kernelShutdownBegin(void);

KernelState kernelGetState(void);

bool kernelReaderPidCanAdd(void);
bool kernelReaderPidAdd(ProcManPid pid);
bool kernelReaderPidRemove(ProcManPid pid);

#endif
