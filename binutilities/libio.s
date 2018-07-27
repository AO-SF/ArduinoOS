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

; Print unsigned decimal routine (fd=r0, x=r1)
label printDec
push r0 ; save fd
mov r0 r1 ; move value inro r0

mov r1 10000 ; divisor
mov r2 0 ; printFlag - set once we have found a non-zero digit

label printDecLoopStart
div r3 r0 r1 ; division to get most significant digit as the quotient

; Decide if we need to print this digit
cmp r4 r3 r3 ; if this digit is non-zero it needs printing
skipeqz r4
jmp printDecLoopPrintStart
cmp r4 r2 r2 ; if we have already found a significant digit, we need to print this one
skipeqz r4
jmp printDecLoopPrintStart
mov r4 1 ; if this is the last sigit it always needs printing
cmp r4 r1 r4
skipneq r4
jmp printDecLoopPrintStart
jmp printDecLoopPrintEnd ; otherwise avoid printing insignificant zeros

label printDecLoopPrintStart
; protect x, divisor and digit
push r0
push r1
push r3
; print digit
mov r2 '0' ; add '0' to make ascii character
add r3 r3 r2
mov r2 libIoScratchByte
store8 r2 r3 ; store this character into ram for the write syscall to access
mov r0 257 ; write syscall
pop r1 ; fd
push r1
mov r3 1
syscall
; restore values
pop r3
pop r1
pop r0
; set print flag
mov r2 1
label printDecLoopPrintEnd

; Stop once divisor=1
mov r4 1
cmp r4 r1 r4
skipeq r4
jmp printDecLoopNext ; This is here because skip only skips one true instruction, while ret is pseudo and takes many true instructions
ret

label printDecLoopNext
mul r3 r3 r1 ; take this digits value away from x
sub r0 r0 r3
mov r3 10 ; reduce divisor by factor 10 ready for next iteration
div r1 r1 r3
jmp printDecLoopStart
