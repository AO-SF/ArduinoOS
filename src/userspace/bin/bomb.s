; a minimal fork bomb
nostack
require lib/sys/syscall.s
label start
mov r0 SyscallIdFork
syscall
jmp start
