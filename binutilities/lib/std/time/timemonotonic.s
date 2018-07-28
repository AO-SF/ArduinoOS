; r0=gettimemonotonic() returns number of seconds since booting
label gettimemonotonic
mov r0 768
syscall
ret
