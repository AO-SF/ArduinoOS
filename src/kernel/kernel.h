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

// The following two functions make it possible for kernel space code to use the spi bus without having to go via the VFS
bool kernelSpiGrabLock(uint8_t slaveSelectPin);
void kernelSpiReleaseLock(void);

#endif
