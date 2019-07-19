; a minimal fork bomb
nostack
require lib/sys/syscall.s
mov r0 SyscallIdFork
syscall
mov r7 0
