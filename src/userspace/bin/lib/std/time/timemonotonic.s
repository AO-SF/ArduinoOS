require ../../sys/syscall.s

; r0=gettimemonotonic() returns number of seconds since booting
label gettimemonotonic
mov r0 SyscallIdTimeMonotonic
syscall
ret
