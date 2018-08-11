; r0=strtoint(str=r0) returns 0 on bad input
label strtoint

mov r1 0 ; total

label strtointLoopStart
; Read character and get digit value
load8 r2 r0

mov r3 '0'
sub r2 r2 r3

mov r3 0
cmp r3 r2 r3
skipge r3
jmp strtointLoopEnd
mov r3 9
cmp r3 r2 r3
skiple r3
jmp strtointLoopEnd

; Add digit to total
mov r3 10
mul r1 r1 r3
add r1 r1 r2

; Advance to next character
inc r0
jmp strtointLoopStart
label strtointLoopEnd

mov r0 r1

ret
