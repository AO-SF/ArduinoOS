jmp start

require libio.s
require libproc.s

ab pwdBuf 64

label start

; Grab pwd
mov r0 pwdBuf
call getpwd

; Print pwd and newline
mov r0 pwdBuf
call puts

mov r0 '\n'
call putc

; Exit
mov r0 0
call exit
