require libiofput.s

; putdec(x=r0)
label putdec
mov r1 r0
mov r0 512
syscall
jmp fputdec

; fputdec(fd=r0, x=r1)
label fputdec

; Init ready for loop
mov r2 10000
mov r4 0 ; foundDigit flag

; Divide to find current digit
label fputdecLoopStart
div r3 r1 r2 ; current digit in r3

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
push r0
push r1
push r2
push r3
push r4
mov r1 '0'
add r1 r1 r3
call fputc
pop r4
pop r3
pop r2
pop r1
pop r0

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

ret
