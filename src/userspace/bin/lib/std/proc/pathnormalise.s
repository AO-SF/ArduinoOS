require ../../sys/syscall.s

; pathnormalise(r0=addr) - simplifies path (e.g. converting '/bin/../' to '/')
label pathnormalise
mov r1 r0
mov r0 SyscallIdPathNormalise
syscall
ret
