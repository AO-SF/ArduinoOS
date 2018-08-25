require fput.s

; puthex(x=r0) - takes 16 bit unsigned int and prints as 4 hex digits to stdout
label puthex
mov r1 r0
mov r0 512
syscall
jmp fputhex

; fputhex(fd=r0, x=r1) - takes 16 bit unsigned int and prints as 4 hex digits to given fd
label fputhex
; digit 1
push8 r0
push16 r1
mov r2 12
shr r1 r1 r2
call fputhexdigit
pop16 r1
pop8 r0
; digit 2
push8 r0
push16 r1
mov r2 8
shr r1 r1 r2
mov r2 15
and r1 r1 r2
call fputhexdigit
pop16 r1
pop8 r0
; digit 3
push8 r0
push16 r1
mov r2 4
shr r1 r1 r2
mov r2 15
and r1 r1 r2
call fputhexdigit
pop16 r1
pop8 r0
; digit 4
mov r2 15
and r1 r1 r2
call fputhexdigit
ret

; fputhexdigit(fd=r0, d=r1) - takes 4 bit value and prints as hex character to given fd
label fputhexdigit
mov r3 10
cmp r3 r1 r3
mov r2 '0'
add r1 r1 r2
skiplt r3
inc7 r1
call fputc0
ret
