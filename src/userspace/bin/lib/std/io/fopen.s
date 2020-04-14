require ../../sys/syscall.s

; fd=fopen(path=r0, mode=r1) - places fd in r0, returns FdInvalid (0) on failure
label fopen
; Use open syscall
mov r2 r1
mov r1 r0
mov r0 SyscallIdOpen
syscall
ret
