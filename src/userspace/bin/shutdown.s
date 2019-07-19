require lib/sys/syscall.s

; Invoke shutdown syscall
mov r0 SyscallIdShutdown
syscall

; Call exit in case shutdown is not immediate
mov r0 SyscallIdExit
mov r1 0
syscall
