jmp start

require libio.s

ab pwdBuf 64

label start

; Grab pwd
mov r0 514
mov r1 pwdBuf
syscall

; Print pwd and newline
mov r0 pwdBuf
call puts

mov r0 '\n'
call putc

; Exit
mov r0 0
mov r1 0
syscall
