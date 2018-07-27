ab libIoScratchByte 1

; fgets(fd=r0, buf=r1, len=r2) reads up to and including first newline, always null-terminates buf (potentially to be 0 length if could not read)
; TODO: Support length limit from r2
label fgets

mov r3 0 ; // loop index

label fgetsLoopStart

; Call fgetc
push r0
push r1
push r3
call fgetc
mov r4 r0 ; r4 contains read character
pop r3
pop r1
pop r0

; Check for failure
mov r5 256
cmp r5 r4 r5
skipneq r5
jmp fgetsLoopEnd

; Copy character
mov r5 r1
add r5 r5 r3
store8 r5 r4
inc r3

; Hit newline?
mov r5 '\n'
cmp r5 r4 r5
skipneq r5
jmp fgetsLoopEnd

; Loop around
jmp fgetsLoopStart

label fgetsLoopEnd

; Add null terminator
mov r5 r1
add r5 r5 r3
mov r4 0
store8 r5 r4

ret

; r0=fgetc(fd=r0), returns 256 on failure
label fgetc

; read single character into libIoScratchByte
mov r1 r0
mov r0 256
mov r2 libIoScratchByte
mov r3 1
syscall

; check for failure
cmp r0 r0 r0
skipeqz r0
jmp fgetcDone

; failed
mov r0 256
ret

label fgetcDone
mov r0 libIoScratchByte
load8 r0 r0
ret

; puts(strAddr=r0)=fputs(stdio, strAddr)
label puts
mov r1 r0 ; Move string address into r1 ready for fputs call
mov r0 512 ; Grab stdio fd and put it in r0
syscall
jmp fputs ; TODO: Is this safe allowing a different function to return? (or should it be rather)

; fputs(fd=r0, strAddr=r1)
label fputs

mov r3 0 ; loop index
mov r4 r1 ; copy str base addr into r4

label fputsLoopStart

; load character
mov r1 r4
add r1 r1 r3
load8 r1 r1

; reached null terminator?
cmp r2 r1 r1
skipneqz r2
jmp fputsDone

; print character
push r0
push r3
push r4

call fputc

pop r4
pop r3
pop r0

inc r3
jmp fputsLoopStart

label fputsDone
ret

; putc(c=r0)=fpuc(stdio, c)
label putc
mov r1 r0
mov r0 512
syscall
jmp fputc

; fputc(fd=r0, c=r1)
label fputc

; store given character into scratch byte for write call to access
mov r2 libIoScratchByte
store8 r2 r1

; syscall write
mov r1 r0 ; move fd before we overwrite it
mov r0 257
mov r2 libIoScratchByte
mov r3 1
syscall

ret

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
