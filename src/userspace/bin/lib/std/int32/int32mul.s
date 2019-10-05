require int32common.s

; int32mul1616(dest=r0, opA=r1, opB=r2) - stores product of two 16-bit values opA and opB into 32-bit dest ptr
label int32mul1616
; Multiply two lower halves to get lower half of product, and set lower part of dest
mov r5 255
and r3 r1 r5
and r4 r2 r5
mul r3 r3 r4 ; r3 now contains lower half product
inc2 r0
store16 r0 r3
dec2 r0
; Multiply two upper halves to get upper half of product, and set upper part of dest
mov r5 8
shr r3 r1 r5
shr r4 r2 r5
mov r5 255
and r3 r3 r5
and r4 r4 r5
mul r3 r3 r4 ; r3 now contains upper half product
store16 r0 r3
; Multiply and add two other pairs to the lower half (which may overflow into the upper)
mov r5 8
shr r3 r1 r5
mov r5 255
and r3 r3 r5
and r4 r2 r5
mul r3 r3 r4 ; r3 now contains ah*bl
push16 r2
push16 r1
push16 r0
mov r0 int32ScratchInt32
mov r1 r3
mov r2 8
call int32set16shift
pop16 r0
push16 r0
mov r1 int32ScratchInt32
call int32add32
pop16 r0
pop16 r1
pop16 r2
mov r5 8
shr r4 r2 r5
mov r5 255
and r3 r1 r5
and r4 r4 r5
mul r3 r3 r4 ; r3 now contains al*bh
push16 r2
push16 r1
push16 r0
mov r0 int32ScratchInt32
mov r1 r3
mov r2 8
call int32set16shift
pop16 r0
push16 r0
mov r1 int32ScratchInt32
call int32add32
pop16 r0
pop16 r1
pop16 r2
ret
