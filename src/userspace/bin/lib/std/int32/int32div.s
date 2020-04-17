require ../../sys/syscall.s

; int32div16(r0=x, r1=d) - computes x=x/d where x is ptr to 32 bit value, and d is 16 bit divisor, returns 16 bit remainder in r0
label int32div16
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Div16
syscall
ret

; int32div32(r0=dest, r1=divisor) - divide 32 bit quantity at r0 by 32 bit divisor at r1
label int32div32
; Use stack to store remainder which we then discard
mov r2 r6
inc4 r6
call int32div32rem
dec4 r6
ret

; int32div32rem(r0=dest, r1=divisor, r2=remainder) - divide 32 bit quantity at r0 by 32 bit divisor at r1, setting remainder in the process
label int32div32rem
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Div32
syscall
ret
