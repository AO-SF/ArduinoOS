requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s

db commaSpaceStr ', ',0

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Init sequence
mov r4 0 ; r4 is the lower of the two current values
mov r5 1 ; r5 is the higher

label loopstart
; Print lowest of two values (protecting r4 and r5)
push16 r4
push16 r5
mov r0 r4
call putdec
pop16 r5
pop16 r4

; Time to end?
mov r3 40000
cmp r3 r4 r3
skiplt r3 ; if r4<40000 is true this causes us to skip the next instruction and continue executing the loop
jmp loopend ; but if false then we end up here and break out of the loop

; Print comma and space
push16 r4
push16 r5
mov r0 commaSpaceStr
call puts0
pop16 r5
pop16 r4

; Perform single Fibonacci step
add r3 r4 r5 ; add two numbers and store into temp register
mov r4 r5 ; move higher into lower
mov r5 r3 ; move sum into higher

jmp loopstart

; Print newline to terminate list
label loopend
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
