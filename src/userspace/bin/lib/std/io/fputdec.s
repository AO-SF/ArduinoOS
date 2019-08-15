require fput.s

; putdecpad(x=r0)
label putdecpad
mov r1 r0
mov r0 SyscallIdEnvGetStdoutFd
syscall
mov r2 1
jmp fputdeccommon

; putdec(x=r0)
label putdec
mov r1 r0
mov r0 SyscallIdEnvGetStdoutFd
syscall
jmp fputdec

; fputdec(fd=r0, x=r1)
label fputdec
mov r2 0
jmp fputdeccommon

; fputdeccommon(fd=r0, x=r1, padFlag=r2) - write x in ascii as a decimal value to given fd, optionally padded with zeros
; note: this was originally done with a loop but it is significantly faster unrolled and optimised
label fputdeccommon
; if padding flag set, simply jump straight to printing first digit
cmp r2 r2 r2
skipeqz r2
jmp fputdecprint4
; check if single digit
mov r2 10
cmp r4 r1 r2
skipge r4
jmp fputdecprint0
; check if two digits
mov r3 100
cmp r4 r1 r3
skipge r4
jmp fputdecprint1
; check if three digits
mul r3 r2 r3 ; shorter version of `mov r3 1000`
cmp r4 r1 r3
skipge r4
jmp fputdecprint2
; check if four digits
mul r2 r2 r3 ; shorter version of `mov r2 10000`
cmp r4 r1 r2
skipge r4
jmp fputdecprint3
; else five digits
; print ten-thousands digit
label fputdecprint4
mov r2 10000
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
push8 r0
push16 r1
mov r2 '0'
add r1 r3 r2
call fputc0
pop16 r1
pop8 r0
; print thousands digit
label fputdecprint3
mov r2 1000
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
push8 r0
push16 r1
mov r2 '0'
add r1 r3 r2
call fputc0
pop16 r1
pop8 r0
; print hundreds digit
label fputdecprint2
mov r2 100
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
push8 r0
push8 r1 ; notice from here onwards we can assume x is <256 so fits in 8 bits
mov r2 '0'
add r1 r3 r2
call fputc0
pop8 r1
pop8 r0
; print tens digit
label fputdecprint1
mov r2 10
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
push8 r0
push8 r1
mov r2 '0'
add r1 r3 r2
call fputc0
pop8 r1
pop8 r0
; print units digit
label fputdecprint0
; divisor is one - nothing to do
; also no need to protect registers in this case - we are about to return
mov r2 '0'
add r1 r1 r2
call fputc0
ret
