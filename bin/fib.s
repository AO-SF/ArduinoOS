; Init sequence
mov r5 0
mov r6 1

label loopstart
; Print current value in r5
mov r0 1337 ; temporary print value syscall (TODO: write a printDec function)
mov r1 r5
syscall

; Time to end?
mov r3 40000
cmp r3 r5 r3
skiplt r3 ; if r5<40000 is true this causes us to skip the next instruction and continue executing the loop
jmp loopend ; but if false then we end up here and break out of the loop

; Perform single Fibonacci step
add r4 r5 r6
mov r5 r6
mov r6 r4

jmp loopstart
label loopend

; Exit
mov r0 2
mov r1 0
syscall
