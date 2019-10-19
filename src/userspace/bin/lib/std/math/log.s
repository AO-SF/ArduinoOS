; r0=log2(r0) - returns floor(log2(r0)), returning 0 for r0=0 as a special case
label log2
; Count number of leading zeros
clz r0
; If r0=0 then clz returns 16 so decrement it to match r0=1 case
mov r1 15
cmp r2 r0 r1
skiple r2
dec r0
; Subtract clz from 15 to get log2 result
sub r0 r1 r0
ret
