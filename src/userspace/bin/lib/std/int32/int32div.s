requireend int32add.s
requireend int32cmp.s
requireend int32log.s
requireend int32set.s
requireend int32shift.s
requireend int32sub.s

; TODO: try to do with fewer bytes of global ram used
aw int32div32remDivisor 2
aw int32div32remMultiplier 2
aw int32div32remRemainder 2
aw int32div32remResult 2

; int32div16(r0=x, r1=d) - computes x=x/d where x is ptr to 32 bit value, and d is 16 bit divisor
label int32div16
; Use stack to store a 32 bit version of the given divisor
mov r2 r6
inc4 r6
push16 r0 ; protect dest ptr
push16 r2 ; protect 32 bit divisor ptr
mov r0 r2
call int32set16
pop16 r1 ; restore divisor ptr
pop16 r0 ; restore dest ptr
; Call standard 32 bit division function
call int32div32
; Restore stack
dec4 r6
ret

; int32div32(r0=dest, r1=divisor) - divide 32 bit quantity at r0 by 32 bit divisor at r1
label int32div32
; We will use int32div32rem to do most of the work
; So first reserve a dummy 32 bit int on the stack to act as the remainder argument
mov r2 r6
inc4 r6
; Call int32div32rem to do the division
call int32div32rem
; Restore stack
dec4 r6
ret

; int32div32rem(r0=dest, r1=divisor, r2=remainder) - divide 32 bit quantity at r0 by 32 bit divisor at r1, setting remainder in the process
label int32div32rem
; Protect 'result' arguments we will need later
push16 r0
push16 r2

; Setup variables
push16 r1 ; protect divisor
mov r1 r0
mov r0 int32div32remRemainder
call int32set32
mov r0 int32div32remDivisor
pop16 r1 ; restore divisor
call int32set32
mov r0 int32div32remMultiplier
mov r1 1
call int32set16
mov r0 int32div32remResult
mov r1 0
call int32set16

; Shift divisor left until MSB is 1, and shift multiplier by same amount
mov r0 int32div32remDivisor
call int32clz
mov r1 r0
push8 r1
mov r0 int32div32remDivisor
call int32ShiftLeft
pop8 r1
mov r0 int32div32remMultiplier
call int32ShiftLeft

; Division loop
label int32div32remLoopStart

; Is current value less than divisor so that we cannot subtract?
mov r0 int32div32remRemainder
mov r1 int32div32remDivisor
call int32LessThan
cmp r0 r0 r0
skipeqz r0
jmp int32div32remLoopNext

; We can subtract - do so and update result
mov r0 int32div32remRemainder
mov r1 int32div32remDivisor
call int32sub32
mov r0 int32div32remResult
mov r1 int32div32remMultiplier
call int32add32

; Prepare for next iteration (divide divisor and multiplier by 2)
label int32div32remLoopNext
mov r0 int32div32remDivisor
call int32ShiftRight1
mov r0 int32div32remMultiplier
call int32ShiftRight1

; If multiplier is 0 then we are done,
; otherwise loop again.
mov r0 int32div32remMultiplier
mov r1 Int32Const0
call int32Equal
cmp r0 r0 r0
skipneqz r0
jmp int32div32remLoopStart

; Restore arguments and copy results into them
pop16 r0 ; restore remainder
mov r1 int32div32remRemainder
call int32set32

pop16 r0 ; restore dest
mov r1 int32div32remResult
call int32set32

; Return
ret
