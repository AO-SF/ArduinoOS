db newline '\n'

; Init sequence
mov r4 0 ; r4 is the lower of the two current values
mov r5 1 ; r5 is the higher

label loopstart
; Print lowest of two values (protecting r4 and r5)
push r4
push r5
mov r0 r4
call printDec
pop r5
pop r4

mov r0 258 ; write syscall (from progmem)
mov r1 1 ; stdout fd
mov r2 newline
mov r3 1
syscall

; Time to end?
mov r3 40000
cmp r3 r4 r3
skiplt r3 ; if r4<40000 is true this causes us to skip the next instruction and continue executing the loop
jmp loopend ; but if false then we end up here and break out of the loop

; Perform single Fibonacci step
add r3 r4 r5 ; add two numbers and store into temp register
mov r4 r5 ; move higher into lower
mov r5 r3 ; move sum into higher

jmp loopstart
label loopend

; Exit
mov r0 0
mov r1 0
syscall

; Print decimal routine (takes value from r0)
label printDec
mov r1 10000 ; divisor
mov r2 0 ; print flag set once we have found a non-zero digit

label printDecLoopStart
div r3 r0 r1 ; division to get most significant digit

cmp r4 r3 r3
skipeqz r4 ; if this digit is zero it may not need printing
jmp printDecLoopPrintStart
cmp r4 r2 r2
skipeqz r4 ; if we have not yet found any significant digits, we may not need to print this one
jmp printDecLoopPrintStart
mov r4 1
cmp r4 r1 r4
skipneq r4 ; if this not the last digit we do not have to print it
jmp printDecLoopPrintStart
jmp printDecLoopPrintEnd ; otherwise avoid printing insignificant 0

label printDecLoopPrintStart
; protect x, divisor and digit
push r0
push r1
push r3
; print digit
mov r2 '0'
add r3 r3 r2
mov r2 1024 ; TODO: Use dw to create a symbol for this
store8 r2 r3
mov r0 257 ; write syscall
mov r1 1 ; stdout fd
mov r3 2
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
mul r3 r3 r1 ; take this digits value away from r0
sub r0 r0 r3
mov r3 10 ; reduce divisor by factor 10 ready for next iteration
div r1 r1 r3
jmp printDecLoopStart
