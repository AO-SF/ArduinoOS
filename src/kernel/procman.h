#ifndef PROCMAN_H
#define PROCMAN_H

#include <stdint.h>

#include "bytecode.h"

typedef uint8_t ProcManPid;
#define ProcManPidMax 16

typedef enum {
	ProcManExitStatusSuccess=0, // 0 and all other unused values can be used when calling exit
	ProcManExitStatusInterrupted=65531, // waitpid was interrupted by a signal
	ProcManExitStatusNoProcess=65532, // process does not exist to begin with
	ProcManExitStatusKilled=65534, // process was killed by either a syscall or the kernel itself (rather than the process itself calling exit)
	ProcManExitStatusTimeout=65535, // waitpid timed out (process is still running)
} ProcManExitStatus;

////////////////////////////////////////////////////////////////////////////////
// General functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void);
void procManQuit(void);

void procManTickAll(void);

ProcManPid procManGetProcessCount(void);

////////////////////////////////////////////////////////////////////////////////
// Process functions
////////////////////////////////////////////////////////////////////////////////

ProcManPid procManProcessNew(const char *programPath); // Returns ProcManPidMax on failure

void procManKillAll(void);
void procManProcessKill(ProcManPid pid, ProcManExitStatus exitStatus);

void procManProcessTick(ProcManPid pid);

void procManProcessSendSignal(ProcManPid pid, BytecodeSignalId signalId);

bool procManProcessExists(ProcManPid pid);

#endif
