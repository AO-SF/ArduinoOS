require ../../sys/syscall.s

; fclose(fd=r0)
label fclose
; Invoke close syscall
mov r1 r0
mov r0 SyscallIdClose
syscall
ret
