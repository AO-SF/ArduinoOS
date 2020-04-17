require int32common.s

; int32mod32(x=r0, y=r1) - stores remainder of x/y into x (both operands pointers to 32 bit values)
label int32mod32
; We could compute via x-(x/y)*x, but instead use division function which also returns remainder
; Reserve space on stack for remainder
mov r2 r6
inc4 r6
; Call int32div32rem which does x=x/y, storing remainder onto stack
push16 r0
call int32div32rem
pop16 r0
; Copy remainder into x
mov r1 r6
dec4 r1
call int32set32
; Restore stack
dec4 r6
ret
