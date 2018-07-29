require fput.s
require fputdec.s

; puttime(time=r0)
label puttime
mov r1 r0
mov r0 512
syscall
jmp fputtime

; fputtime(fd=r0, time=r1)
label fputtime

; Divide by 3600 to test for hours
mov r2 3600
div r2 r1 r2

cmp r3 r2 r2
skipneqz r3
jmp minutes

push r2
push r1
push r0

mov r1 r2
call fputdec

pop r0
push r0
mov r1 'h'
call fputc0

pop r0
pop r1
pop r2

mov r3 3600
mul r2 r2 r3
sub r1 r1 r2

; Divide by 60 to test for minutes
label minutes

mov r2 60
div r2 r1 r2

cmp r3 r2 r2
skipneqz r3
jmp seconds

push r2
push r1
push r0

mov r1 r2
call fputdec

pop r0
push r0
mov r1 'm'
call fputc0

pop r0
pop r1
pop r2

mov r3 60
mul r2 r2 r3
sub r1 r1 r2

; Seconds
label seconds
push r0

call fputdec

pop r0
mov r1 's'
call fputc0

ret
