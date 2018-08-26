require ../../sys/syscall.s

; exit (r0=status) - does not return
label exit
mov r1 r0
mov r0 SyscallIdExit
syscall
