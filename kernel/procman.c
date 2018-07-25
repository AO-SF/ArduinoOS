#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "bytecode.h"
#include "kernelfs.h"
#include "procman.h"

#define ProcManProcessRamSize 128 // TODO: Allow this to be dynamic

typedef struct {
	ByteCodeWord regs[BytecodeRegisterNB];
	bool skipFlag; // skip next instruction?
	uint8_t ram[ProcManProcessRamSize];
} ProcManProcessTmpData;

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
ProcManPid procManGetPidFromProcess(ProcManProcess *process);

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

void procManTickAll(void) {
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		procManProcessTick(pid);
}

int procManGetProcessCount(void) {
	int count=0;
	ProcManPid pid;
	for(pid=0; pid<ProcManPidMax; ++pid)
		count+=(procManGetProcessByPid(pid)!=NULL);
	return count;
}

ProcManPid procManProcessNew(const char *programPath) {
	// Find a PID for the new process
	ProcManPid pid=procManFindUnusedPid();
	if (pid==ProcManPidMax)
		goto error;

	// Construct tmp file path
	// TODO: Try others if exists
	char tmpPath[KernelFsPathMax];
	sprintf(tmpPath, "/tmp/proc%u", pid);

	// Attempt to open program file
	procManData.processes[pid].progmemFd=kernelFsFileOpen(programPath);
	if (procManData.processes[pid].progmemFd==KernelFsFdInvalid)
		goto error;

	// Attempt to create tmp file
	// TODO: Ensure size is right (either on creation by specifying size, or by filling ram with say 0xFF)
	if (!kernelFsFileCreate(tmpPath))
		goto error;

	// Attempt to open tmp file
	procManData.processes[pid].tmpFd=kernelFsFileOpen(tmpPath);
	if (procManData.processes[pid].tmpFd==KernelFsFdInvalid)
		goto error;

	// Initialise tmp file:
	ProcManProcessTmpData procTmpData;
	procTmpData.regs[ByteCodeRegisterIP]=0;
	procTmpData.skipFlag=false;

	KernelFsFileOffset written=kernelFsFileWrite(procManData.processes[pid].tmpFd, (const uint8_t *)&procTmpData, sizeof(procTmpData));
	if (written<sizeof(procTmpData))
		goto error;

	return pid;

	error:
	if (pid!=ProcManPidMax) {
		kernelFsFileClose(procManData.processes[pid].progmemFd);
		procManData.processes[pid].progmemFd=KernelFsFdInvalid;
		kernelFsFileClose(procManData.processes[pid].tmpFd);
		procManData.processes[pid].tmpFd=KernelFsFdInvalid;
		kernelFsFileDelete(tmpPath); // TODO: If we fail to even open the programPath then this may delete a file which has nothing to do with us
	}

	return ProcManPidMax;
}

void procManProcessTick(ProcManPid pid) {
	// Find process from PID
	ProcManProcess *process=procManGetProcessByPid(pid);
	if (process==NULL)
		return;

	// TODO: this
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

ProcManProcess *procManGetProcessByPid(ProcManPid pid) {
	if (procManData.processes[pid].progmemFd!=KernelFsFdInvalid)
		return procManData.processes+pid;
	return NULL;
}

ProcManPid procManGetPidFromProcess(ProcManProcess *process) {
	return process-procManData.processes;
}

ProcManPid procManFindUnusedPid(void) {
	for(int i=0; i<ProcManPidMax; ++i)
		if (procManData.processes[i].progmemFd==KernelFsFdInvalid)
			return i;
	return ProcManPidMax;
}
