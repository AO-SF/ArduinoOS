; init - run startup file followed by a shell, while continuing to run indefintely with pid 0

requireend lib/std/proc/exit.s
requireend lib/std/proc/forkexec.s
requireend lib/std/proc/forkexecwait.s

db ttyPath '/dev/ttyS0', 0
db startupPath '/etc/startup', 0
db shutdownPath '/etc/shutdown', 0
db shellPath '/bin/sh', 0

jmp start

; signal handler labels must be within first 256 bytes of executable, so put this function first
label suicideHandler
; call forkexecwait to run shutdown file and wait for it to complete
mov r0 1
mov r1 shutdownPath
call forkexecwait
; Exit ASAP
mov r0 0
call exit

; ensure we have pid 0 (otherwise init should already be running)
label start
mov r0 SyscallIdGetPid
syscall
cmp r0 r0 r0
skipeqz r0
jmp error

; Register suicide signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; Open stdin and stdout (these will naturally use fds 1 and 2)
mov r0 SyscallIdOpen
mov r1 ttyPath
mov r2 FdModeRO ; read only for stdin
syscall

mov r0 SyscallIdOpen
mov r1 ttyPath
mov r2 FdModeWO ; write only for stdout
syscall

; call forkexecwait to run startup file and wait for it to complete
mov r0 1
mov r1 startupPath
call forkexecwait

; call forkexec to start shell
mov r0 1
mov r1 shellPath
call forkexec

; error forking?
cmp r0 r0 r0
skipneqz r0
jmp error

; otherwise call waitpid with our own pid so that we sleep forever
label parent
mov r0 SyscallIdWaitPid
mov r1 0
mov r2 0 ; infinite timeout
syscall
jmp parent ; in case waitpid fails for any reason

; forkexec error
label error
mov r0 1
call exit
