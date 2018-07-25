#include "kernelfs.h"
#include "procman.h"

typedef struct {
	KernelFsFd progmemFd, tmpFd;
} ProcManProcess;

typedef struct {
	ProcManProcess processes[ProcManPidMax];
} ProcMan;
ProcMan procManData;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid);

ProcManPid procManFindUnusedPid(void);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void) {
	// Clear fds
	for(int i=0; i<ProcManPidMax; ++i) {
		procManData.processes[i].progmemFd=KernelFsFdInvalid;
		procManData.processes[i].tmpFd=KernelFsFdInvalid;
	}
}

void procManQuit(void) {
	// Close all FDs
	for(int i=0; i<ProcManPidMax; ++i) {
		if (procManData.processes[i].progmemFd!=KernelFsFdInvalid) {
			kernelFsFileClose(procManData.processes[i].progmemFd);
			procManData.processes[i].progmemFd=KernelFsFdInvalid;
		}

		if (procManData.processes[i].tmpFd!=KernelFsFdInvalid) {
			kernelFsFileClose(procManData.processes[i].tmpFd);
			procManData.processes[i].tmpFd=KernelFsFdInvalid;
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

ProcManPid procManFindUnusedPid(void) {
	for(int i=0; i<ProcManPidMax; ++i)
		if (procManData.processes[i].progmemFd==KernelFsFdInvalid)
			return i;
	return ProcManPidMax;
}
