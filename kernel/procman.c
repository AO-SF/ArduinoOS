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

ProcManProcess *procManGetProcessByPid(ProcManPid pid);

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

int procManGetProcessCount(void) {
	int count=0;
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		count+=(procManGetProcessByPid(pid)!=NULL);
	return count;
}

ProcManPid procManProcessNew(const char *programPath) {
	// TODO: this

	return ProcManPidMax;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid) {
	for(int i=0; i<ProcManPidMax; ++i)
		if (procManData.processes[i].progmemFd!=KernelFsFdInvalid)
			return procManData.processes+i;
	return NULL;
}
