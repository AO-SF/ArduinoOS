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

#define kernelTickMinTimeMs 10 // avoids excessive CPU use, and can be tweaked to roughly imitate running on real hardware

#ifndef ARDUINO
extern bool kernelFlagProfile;
#endif

void kernelShutdownBegin(void);

KernelState kernelGetState(void);

// The following two functions make it possible for kernel space code to use the spi bus without having to go via the VFS
bool kernelSpiGrabLock(uint8_t slaveSelectPin);
bool kernelSpiGrabLockNoSlaveSelect(void); // Like grab lock but does not pull any slave select pin low (or high again when released)
void kernelSpiReleaseLock(void);

#endif
