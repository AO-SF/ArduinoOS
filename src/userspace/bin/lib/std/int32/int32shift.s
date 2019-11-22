; int32ShiftLeft16(r0=x)=int32shiftLeft(x, 16) - special case of shift left where shift is fixed at 16 bits
label int32ShiftLeft16
; Move lower into upper, setting lower to 0 in the process
inc2 r0
load16 r1 r0
mov r2 0
store16 r0 r2
dec2 r0
store16 r0 r1
ret

; int32ShiftLeft(r0=x, r1=s) - shift x left by s bits
label int32ShiftLeft
; Load upper and lower words
load16 r2 r0 ; upper word in r2
inc2 r0
load16 r3 r0 ; lower word in r3
; Shift upper part left by given amount
shl r2 r2 r1
; Extract bits from lower and move into upper (if needed)
mov r4 32
cmp r5 r1 r4
skiplt r5
jmp int32ShiftLeftNext
mov r4 16
cmp r5 r1 r4
skiple r5
jmp int32ShiftLeftGreater16
; shift<=16 case
sub r4 r4 r1
shr r4 r3 r4
or r2 r2 r4
jmp int32ShiftLeftNext
; 16<shift<32 case
label int32ShiftLeftGreater16
mov r4 16
sub r4 r1 r4
shl r4 r3 r4
or r2 r2 r4
label int32ShiftLeftNext
; Shift lower part left by given amount
shl r3 r3 r1
; Store new values
store16 r0 r3
dec2 r0
store16 r0 r2
ret

; int32ShiftRight1(r0=x) - shift x right by 1 bit
label int32ShiftRight1
; Load upper and lower words
load16 r1 r0 ; upper word in r1
inc2 r0
load16 r2 r0 ; lower word in r2
; Shift lower word right one
mov r3 1
shr r2 r2 r3
; Place lowest bit of upper word into highest bit of (shifted) lower word
and r3 r1 r3
mov r4 15
shl r3 r3 r4
or r2 r2 r3
; Shift upper part right one
mov r4 1
shr r1 r1 r4
; Store new values
store16 r0 r2
dec2 r0
store16 r0 r1
ret
