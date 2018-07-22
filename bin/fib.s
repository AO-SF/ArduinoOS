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
mov r2 4 ; 4 is shift for less than
shr r3 r3 r2
mov r2 1
and r3 r3 r2 ; r3 now contains 0/1 as result of (r5<40000)
mov r2 3 ; 'jmp loopend' needs 3 bytes (set16 r7 loopendaddr)
mul r3 r3 r2
add r7 r7 r3 ; if r5<40000 is false this is a nop and we break out of the loop on the next instruction,
jmp loopend ; otherwise this jump is skipped and we continue executing the loop

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
