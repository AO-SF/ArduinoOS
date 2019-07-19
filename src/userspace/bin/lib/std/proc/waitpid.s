require ../../sys/syscall.s

label waitpid ; takes pid in r0, uses infinite timeout and handles waitpid being interrupted
; setup registers for waitpid syscall
mov r1 r0 ; childs PID
mov r2 0 ; infinite timeout
mov r3 SyscallWaitpidStatusInterrupted
; loop as waitpid may be interrupted by a signal
label waitpidLoop
mov r0 SyscallIdWaitPid ; as the syscall returns the result in r0 also, we need to reset this each iteration
syscall
cmp r4 r0 r4
skipneq r4 ; interrupted?
jmp waitpidLoop
ret
