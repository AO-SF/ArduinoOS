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

; int32sub32(dest=r0, opA=r1) - both arguments pointers to 32 bit values
; Subtract upper half of opA from dest
load16 r2 r0
load16 r3 r1
sub r2 r2 r3
store16 r0 r2
; Do we need to borrow to subtract the lower halves?
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipge r4
jmp int32sub32Borrow
; Standard case - subtract lower half of opA from dest
sub r2 r2 r3
store16 r0 r2
ret
; Borrow case
label int32sub32Borrow (lower half of dest in r2, lower half of opA in r3)
; Compute 2^16-opAlow and add this to dest lower half instead
mov r4 65535
xor r3 r3 r4
add r2 r2 r3
store16 r0 r2
; Decrement upper half of dest (this is the 'borrow')
dec2 r0
load16 r2 r0
dec r2
store16 r0 r2
ret
