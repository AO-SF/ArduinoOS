; 32 bit operands require pointers to 4 bytes (or 2 words)

; int32set16(dest=r0, src=r1)
label int32set16
mov r2 0
store16 r0 r2
inc2 r0
store16 r0 r1
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
