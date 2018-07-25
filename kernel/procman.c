#include "kernelfs.h"
#include "procman.h"

typedef struct {
	KernelFsFd progmemFd, ramFd;
} ProcManProcess;

typedef struct {
	ProcManProcess processes[ProcManPidMax];
} ProcMan;

ProcMan procManData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void) {
	// Clear fds
	for(int i=0; i<ProcManPidMax; ++i) {
		procManData.processes[i].progmemFd=KernelFsFdInvalid;
		procManData.processes[i].ramFd=KernelFsFdInvalid;
	}
}

void procManQuit(void) {
	// Close all FDs
	for(int i=0; i<ProcManPidMax; ++i) {
		if (procManData.processes[i].progmemFd!=KernelFsFdInvalid) {
			kernelFsFileClose(procManData.processes[i].progmemFd);
			procManData.processes[i].progmemFd=KernelFsFdInvalid;
		}

		if (procManData.processes[i].ramFd!=KernelFsFdInvalid) {
			kernelFsFileClose(procManData.processes[i].ramFd);
			procManData.processes[i].ramFd=KernelFsFdInvalid;
		}
	}
}

ProcManPid procManProcessNew(const char *programPath) {
	// TODO: this

	return ProcManPidMax;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////
