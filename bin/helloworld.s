db message 'Hello world!\n'
dw messageLen 13

; Print message from program memory
mov r0 1 ; write syscall (version which uses program memory)
mov r1 0 ; stdout fd
mov r2 message
mov r3 messageLen
syscall

; Exit
mov r0 2
mov r1 0
syscall
