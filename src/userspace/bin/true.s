nostack

require lib/sys/syscall.s

; Exit with zero for success
mov r0 SyscallIdExit
mov r1 0
syscall
