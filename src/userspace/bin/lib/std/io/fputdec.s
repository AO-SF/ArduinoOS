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

; fputdeccommon(fd=r0, x=r1, padFlag=r2)
label fputdeccommon

; Save padFlag onto stack
push8 r2

; Init ready for loop
mov r2 10000
mov r4 0 ; foundDigit flag

; Divide to find current digit
label fputdecLoopStart
div r3 r1 r2 ; current digit in r3

; If padding we need to print all digits
pop8 r5
push8 r5
cmp r5 r5 r5
skipeqz r5
jmp fputdecLoopPrint

; If this digit is non-zero we need to print it
cmp r5 r3 r3
skipeqz r5
jmp fputdecLoopPrint

; If we have already found a significant digit we need to print this one
cmp r5 r4 r4
skipeqz r5
jmp fputdecLoopPrint

; If this is the last digit we also need to print it regardless
mov r5 1
cmp r5 r2 r5
skipneq r5
jmp fputdecLoopPrint

; Otherwise no need to print
jmp fputdecLoopNext

; Print digit
label fputdecLoopPrint
push8 r0
push16 r1
push16 r2
push8 r3
push8 r4
mov r1 '0'
add r1 r1 r3
call fputc0 ; TODO: Add offset argument to fputdec
pop8 r4
pop8 r3
pop16 r2
pop16 r1
pop8 r0

mov r4 1 ; set foundDigit flag

; Take digits value away from x
label fputdecLoopNext
mul r3 r3 r2
sub r1 r1 r3

; Finished? (factor=1)
mov r3 1
cmp r3 r2 r3
skipneq r3
jmp fputdecLoopEnd

; Reduce divisor by factor of 10
mov r3 10
div r2 r2 r3

; Loop to print next digit
jmp fputdecLoopStart

label fputdecLoopEnd
; Restore stack
dec r6 ; remove padFlag from stack
ret
