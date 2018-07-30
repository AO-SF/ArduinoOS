aw int32ScratchInt32 2

; 32 bit operands require pointers to 4 bytes (or 2 words)

; int32set16(dest=r0, src=r1) - 32 bit dest ptr = 16 bit src value
label int32set16
mov r2 0
store16 r0 r2
inc2 r0
store16 r0 r1
ret

; int32setUpper16(dest=r0, src=r1) - 32 bit dest ptr = (16 bit src value)<<16
label int32setUpper16
store16 r0 r1
inc2 r0
mov r2 0
store16 r0 r2
ret

; int32set16shift(dest=r0, src=r1, shift=r2) - like int32set16 but also shifts src first
label int32set16shift
mov r3 16
sub r3 r3 r2
shr r3 r1 r3
store16 r0 r3
inc2 r0
shl r3 r1 r2
store16 r0 r3
ret

; int32add16(dest=r0, src=r1)
label int32add16
; Compute maximum we can add to dest.lower before overflowing
inc2 r0
load16 r2 r0
dec2 r0

mov r3 65535
sub r2 r3 r2 ; r2 has maximum we can add before overflow

cmp r3 r1 r2
skipgt r3
jmp int32add16finaladd

; We cannot add without overflowing, so add what we can, then add one more,
; overflowing (inc dest.upper, set dest.lower=0, and reduce src)
sub r1 r1 r2
dec r1

load16 r2 r0
inc r2
store16 r0 r2

inc2 r0
mov r2 0
store16 r0 r2
dec2 r0

; Add without overflow (including remaining part if there would have been an overflow)
label int32add16finaladd
inc2 r0
load16 r2 r0
add r2 r2 r1
store16 r0 r2

ret

; int32add32(dest=r0, opA=r1)
label int32add32

; Add upper part (not worrying about overflow)
load16 r2 r0
load16 r3 r1
add r2 r2 r3
store16 r0 r2

; Add lower part (which may overflow into upper part)
inc2 r1
load16 r1 r1
call int32add16

ret

; int32sub16(dest=r0, opA=r1)
label int32sub16

; Max we can subtract naively is dest lower
; so see if this is enough
inc2 r0
load16 r2 r0 ; r2 contains dest lower, the max we can subtract initially
dec2 r0

cmp r3 r1 r2
skipgt r3
jmp int32sub16finalsub

; We can not subtract everything in one go
; reduce opA by amount we can handle currently
sub r1 r1 r2

; set dest lower to 2^16-1
mov r2 65535
inc2 r0
store16 r0 r2
dec2 r0

; decrement dest upper
load16 r2 r0
dec r2
store16 r0 r2

label int32sub16finalsub
inc2 r0
load16 r2 r0
sub r2 r2 r1
store16 r0 r2

ret

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

push r2
push r1
push r0
mov r0 int32ScratchInt32
mov r1 r3
mov r2 8
call int32set16shift
pop r0
push r0
mov r1 int32ScratchInt32
call int32add32
pop r0
pop r1
pop r2

mov r5 8
shr r4 r2 r5
mov r5 255
and r3 r1 r5
and r4 r4 r5
mul r3 r3 r4 ; r3 now contains al*bh
push r2
push r1
push r0
mov r0 int32ScratchInt32
mov r1 r3
mov r2 8
call int32set16shift
pop r0
push r0
mov r1 int32ScratchInt32
call int32add32
pop r0
pop r1
pop r2

ret
