; int32sub16(dest=r0, opA=r1)
label int32sub16
; Max we can subtract naively is dest lower, so see if this is enough
inc2 r0
load16 r2 r0 ; r2 contains dest lower, the max we can subtract initially
dec2 r0
cmp r3 r1 r2
skipgt r3
jmp int32sub16finalsub
; We can not subtract everything in one go, reduce opA by amount we can handle currently
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

; int32sub32(dest=r0, opA=r1) - performs dest=dest-opA, where both arguments are pointers to 32 bit values
label int32sub32
; Are we able to subtract lower parts,
; or do we need to borrow/carry from upper?
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skiplt r4
jmp int32sub32NoBorrow
; Need to borrow/carry from upper part of dest
; Note: instead of doing (64k+dest.lower)-opA.lower -
; which would overflow our 16 bit registers -
; we do (64k-opA.lower)+dest.lower
mov r4 65535
dec r3
xor r4 r3 r4 ; calculate 64k-opA.lower
add r4 r4 r2 ; add dest.lower
store16 r0 r4 ; update dest.lower
; Also decrement dest.upper
dec2 r0
load16 r2 r0
dec r2
store16 r0 r2
jmp int32sub32UpperHalves
; No need to borrow - dest.lower is at least opA.lower,
; so simply subtract lower halves
label int32sub32NoBorrow
sub r4 r2 r3
store16 r0 r4
; Move onto upper halves (nothing to borrow from here)
dec2 r0
label int32sub32UpperHalves
dec2 r1
load16 r2 r0
load16 r3 r1
sub r4 r2 r3
store16 r0 r4
; Done
ret
