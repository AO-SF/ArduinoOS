jmp start

require stdlib/libiofput.s
require stdlib/libiofputdec.s
require stdlib/libprocexit.s

label start
; Init sequence
mov r4 0 ; r4 is the lower of the two current values
mov r5 1 ; r5 is the higher

label loopstart
; Print lowest of two values (protecting r4 and r5)
push r4
push r5
mov r0 r4
call putdec
pop r5
pop r4

; Time to end?
mov r3 40000
cmp r3 r4 r3
skiplt r3 ; if r4<40000 is true this causes us to skip the next instruction and continue executing the loop
jmp loopend ; but if false then we end up here and break out of the loop

; Print comma
push r4
push r5
mov r0 ','
call putc
mov r0 ' '
call putc
pop r5
pop r4

; Perform single Fibonacci step
add r3 r4 r5 ; add two numbers and store into temp register
mov r4 r5 ; move higher into lower
mov r5 r3 ; move sum into higher

jmp loopstart

; Print newline to terminate list
label loopend
mov r0 '\n'
call putc

; Exit
mov r0 0
call exit
