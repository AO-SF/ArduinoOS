require ../../sys/syscall.s

; result=fflush(path=r0) - flushes any changes to file/directory at given path, returning 1/0 in r0 for true/false
label fflush
; Use flush syscall
mov r1 r0
mov r0 SyscallIdFlush
syscall
ret
