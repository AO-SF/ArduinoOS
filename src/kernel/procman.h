#ifndef PROCMAN_H
#define PROCMAN_H

#include <stdint.h>

typedef uint8_t ProcManPid;
#define ProcManPidMax 64

typedef enum {
	ProcManExitStatusSuccess=0, // 0 and all other unused values can be used when calling exit
	ProcManExitStatusKilled=65534, // process was killed by either a syscall or the kernel itself (rather than the process itself calling exit)
	ProcManExitStatusTimeout=65535, // waitpid timed out (process is still running)
} ProcManExitStatus;

////////////////////////////////////////////////////////////////////////////////
// General functions
////////////////////////////////////////////////////////////////////////////////

void procManInit(void);
void procManQuit(void);

void procManTickAll(void);

int procManGetProcessCount(void);

////////////////////////////////////////////////////////////////////////////////
// Process functions
////////////////////////////////////////////////////////////////////////////////

ProcManPid procManProcessNew(const char *programPath); // Returns ProcManPidMax on failure
void procManProcessKill(ProcManPid pid, ProcManExitStatus exitStatus);

void procManProcessTick(ProcManPid pid);

#endif