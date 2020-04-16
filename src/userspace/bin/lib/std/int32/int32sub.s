require ../../sys/syscall.s

; int32sub16(dest=r0, opA=r1)
label int32sub16
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Sub16
syscall
ret

; int32sub32(dest=r0, opA=r1) - performs dest=dest-opA, where both arguments are pointers to 32 bit values
label int32sub32
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Sub32
syscall
ret
