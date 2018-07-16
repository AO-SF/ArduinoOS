db message 'Hello world!\n'
dw messageLen 13

; Print 'Hello world!'
set r0 1 ; write syscall
set r1 0 ; stdout
set r2 message
set r3 messageLen
syscall

; Exit
set r0 2
set r1 0
syscall
