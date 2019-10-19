require int32common.s

requireend int32cmp.s

; Array used to lookup first guess to log10(x) based on clz(x)
; note: first two entries should be 9 in theory,
; but this would cause an error on calling int32exp10.
; however, we automatically correct value up to 9 anyway as x>=10^(8+1)
db int32log10ArrayClztoLog10Guess 8,8,8,8,8,7,7,7,6,6,6,6,5,5,5,4,4,4,3,3,3,3,2,2,2,1,1,1,0,0,0,0,0

; int32clz(x=r0) - takes pointer to 32 bit value in x, returns number of leading zeros in r0 (in the interval [0,32])
label int32clz
; move x into r1 so we can use r0 for return value
mov r1 r0
; test for any set bits in upper word
load16 r0 r1
clz r0 r0
mov r2 16
cmp r2 r0 r2
skipeq r2
ret
; upper word is all zeros, use lower word count plus 16
inc2 r1
load16 r0 r1
dec2 r1
clz r0 r0
inc16 r0
ret

; int32log2(x=r0) -returns floor(log2(x)) in r0 (as standard 16 bit value), where x is a pointer to a 32 bit value. returns 0 for x=0 as a special case
label int32log2
; call clz to get number of leading zeros
call int32clz
; if x=0 then clz returns 32, so adjust it to 31 to match x=1 as per specification
mov r1 31
cmp r2 r0 r1
skiple r2
dec r0
; subtract clz from 31 to get log2 value
sub r0 r1 r0
ret

; int32log10(x=r0) - returns floor(log10(x)) in r0 (as standard 16 bit value), where x is a pointer to a 32 bit value. returns 0 for x=0 as a special case
label int32log10
; Call clz and use it to lookup a first guess
push16 r0
call int32clz
mov r1 int32log10ArrayClztoLog10Guess
add r0 r0 r1
load8 r2 r0
pop16 r0
; r2 now contains first guess at log10(x),
; but it may need incrementing to be correct
; (while r0 still contains x)
push8 r2 ; protect first guess
push16 r0 ; protect x pointer
mov r0 r2
inc r0
call int32exp10 ; r0 now contains pointer to 10^(first guess + 1)
pop16 r1 ; restore x pointer into r1
call int32LessEqual
pop8 r1 ; restore first guess
; r0 now contains 1 if first guess needs incrementing, 0 if not
add r0 r0 r1
ret
