require ../../sys/syscall.s
requireend forkexec.s

label forkexecwait ; takes path in r0 to exec in a forked process, with arguments in r1-r3, waiting until it completes before returning in the parent
; call forkexec to start the child
call forkexec
; check for bad fork
cmp r1 r0 r0
skipneqz r1
jmp forkexecwaitret
; wait for child to die
mov r1 r0
mov r0 SyscallIdWaitPid
mov r2 0
syscall
; done
label forkexecwaitret
ret
