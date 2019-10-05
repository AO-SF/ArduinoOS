; int32div16(r0=dest, r1=divisor) - divide 32 bit quantity at r0 by 16 bit divisor in r1
label int32div16
; TODO: Use a better algorithm
mov r2 0 ; result counter
label int32div16loopstart
; Is the value smaller than the divisor?
load16 r3 r0
cmp r3 r3 r3
skipeqz r3
jmp int32div16loopnext
inc2 r0
load16 r3 r0
dec2 r0
cmp r3 r3 r1
skipge r3
jmp int32div16loopend
label int32div16loopnext
; Subtract divisor from dest
push16 r0
push16 r1
push16 r2
call int32sub16
pop16 r2
pop16 r1
pop16 r0
; Inc result counter and loop again
inc r2
jmp int32div16loopstart
; Done
label int32div16loopend
mov r1 r2
call int32set16
ret
