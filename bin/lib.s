ab libScratchByte 1

; Print unsigned decimal routine (takes value x from r0)
label printDec
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
mov r2 libScratchByte
store8 r2 r3 ; store this character into ram for the write syscall to access
mov r0 257 ; write syscall
mov r1 1 ; stdout fd
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

; Print char function (with char c taken from r0)
label printChar
; Store given char into scratch byte as a mini-buffer
mov r1 r0
mov r0 libScratchByte
store8 r0 r1
; Call write syscall with fd=stdout
mov r0 257
mov r1 1
mov r2 libScratchByte
mov r3 1
syscall
ret
