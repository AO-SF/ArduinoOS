require ../../sys/syscall.s

label waitpid ; takes pid in r0, uses infinite timeout and handles waitpid being interrupted
; setup registers for waitpid syscall
mov r1 r0 ; childs PID
mov r2 0 ; infinite timeout
mov r3 SyscallWaitpidStatusInterrupted
; loop as waitpid may be interrupted by a signal
label waitpidLoop
mov r0 SyscallIdWaitpid ; as the syscall returns the result in r0 also, we need to reset this each iteration
syscall
cmp r3 r0 r3
skipneq r3 ; interrupted?
jmp waitpidLoop
ret
