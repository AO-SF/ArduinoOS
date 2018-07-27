jmp start

require stdlib/libiofput.s
require stdlib/libprocgetpwd.s
require stdlib/libprocexit.s

ab pwdBuf 64

label start

; Grab pwd
mov r0 pwdBuf
call getpwd

; Print pwd and newline
mov r0 pwdBuf
call puts0

mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
