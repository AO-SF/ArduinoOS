nostack

require lib/sys/syscall.s

; Exit with non-zero for failure
mov r0 SyscallIdExit
mov r1 1
syscall
