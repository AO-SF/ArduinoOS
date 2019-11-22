require int32common.s

requireend int32add.s
requireend int32set.s

aw int32mul1616ScratchInt32 2

; TODO: use less global ram
aw int32mul32DestPtr 1
aw int32mul32OpAPtr 1

aw int32mul32A 2
aw int32mul32B 2
aw int32mul32C 2

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
mov r0 int32mul1616ScratchInt32
mov r1 r3
mov r2 8
call int32set16shift
pop16 r0
push16 r0
mov r1 int32mul1616ScratchInt32
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
mov r0 int32mul1616ScratchInt32
mov r1 r3
mov r2 8
call int32set16shift
pop16 r0
push16 r0
mov r1 int32mul1616ScratchInt32
call int32add32
pop16 r0
pop16 r1
pop16 r2
ret

; int32mul32(dest=r0, opA=r1) - stores product of two 32-bit values dest and opA into dest
label int32mul32

; Note: below logic does not compute dest.upper*opA.upper product as when this is shifted by 32 bits it is always 0 (i.e. an overflow situation)

; Copy dest and opA
mov r2 int32mul32DestPtr
store16 r2 r0
mov r2 int32mul32OpAPtr
store16 r2 r1

; Compute A=dest.lower*opA.upper
mov r0 int32mul32DestPtr
load16 r0 r0
call int32getLower16
push16 r0

mov r0 int32mul32OpAPtr
load16 r0 r0
call int32getUpper16
pop16 r1

mov r2 r0
mov r0 int32mul32A
call int32mul1616

; Compute B=dest.upper*opA.lower
mov r0 int32mul32DestPtr
load16 r0 r0
call int32getUpper16
push16 r0

mov r0 int32mul32OpAPtr
load16 r0 r0
call int32getLower16
pop16 r1

mov r2 r0
mov r0 int32mul32B
call int32mul1616

; Compute A=(A+B)<<16
mov r0 int32mul32A
mov r1 int32mul32B
call int32add32

mov r0 int32mul32A
call int32ShiftLeft16

; Compute B=dest.lower*opA.lower
mov r0 int32mul32DestPtr
load16 r0 r0
call int32getLower16
push16 r0

mov r0 int32mul32OpAPtr
load16 r0 r0
call int32getLower16
pop16 r1

mov r2 r0
mov r0 int32mul32B
call int32mul1616

; Compute A+=B
mov r0 int32mul32A
mov r1 int32mul32B
call int32add32

; Set dest=A
mov r0 int32mul32DestPtr
load16 r0 r0
mov r1 int32mul32A
call int32set32

ret
