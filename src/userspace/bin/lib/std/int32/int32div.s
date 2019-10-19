requireend int32add.s
requireend int32cmp.s
requireend int32sub.s

aw int32divScratchInt32 2

; int32div16(r0=dest, r1=d) - where dest/src is ptr to 32 bit value, and d is 16 bit divsor
label int32div16
; Use stack to store a 32 bit version of the given divisor
mov r2 r6
inc4 r6
push16 r0 ; protect dest ptr
push16 r2 ; protect 32 bit divisor ptr
mov r0 r2
call int32set16
pop16 r1 ; restore divisor ptr
pop16 r0 ; restore dest ptr
; Call standard 32 bit division function
call int32div32
; Restore stack
dec4 r6
ret

; int32div32(r0=dest, r1=divisor) - divide 32 bit quantity at r0 by 32 bit divisor at r1
label int32div32
; TODO: Use a better algorithm
; Use scratch int32 as result counter, initially set to 0
push16 r0
push16 r1
mov r0 int32divScratchInt32
mov r1 0
call int32set16
pop16 r1
pop16 r0
label int32div32loopstart
; Is the value smaller than the divisor?
push16 r0
push16 r1
call int32LessThan
cmp r2 r0 r0
pop16 r1
pop16 r0
skipeqz r2
jmp int32divloopend
; Subtract divisor from dest
push16 r0
push16 r1
call int32sub32
; Inc result counter and loop again
mov r0 int32divScratchInt32
call int32inc
pop16 r1
pop16 r0
jmp int32div32loopstart
; Done - copy result value into dest
label int32divloopend
mov r1 int32divScratchInt32
call int32set32
ret
