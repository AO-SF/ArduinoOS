require ../../sys/syscall.s

; int32ShiftLeft(r0=x, r1=s) - shift x left by s bits
label int32ShiftLeft
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Shl
syscall
ret

; int32ShiftRight(r0=x, r1=s) - shift x right by s bits
label int32ShiftRight
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Shr
syscall
ret
