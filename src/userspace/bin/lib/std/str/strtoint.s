; r0=strtoint(str=r0) returns 0 on bad input
label strtoint
; Loop over each character in the given string
mov r1 0 ; running total/final value
label strtointLoopStart
; Read single character and get digit value
load8 r2 r0
dec48 r2 ; subtract '0' ascii value to get raw digit value
; Check for invalid character (outside of 0-9 range)
mov r3 0
cmp r3 r2 r3
skipge r3
jmp strtointLoopEnd
mov r3 9
cmp r3 r2 r3
skiple r3
jmp strtointLoopEnd
; Add digit to running total (multiplying existing running total value by 10 first to shift digits left one)
mov r3 10
mul r1 r1 r3
add r1 r1 r2
; Advance to next character
inc r0
jmp strtointLoopStart
label strtointLoopEnd
; Finished - move running total into r0 to return final value
mov r0 r1
ret
