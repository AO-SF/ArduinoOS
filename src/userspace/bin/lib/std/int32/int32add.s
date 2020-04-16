require ../../sys/syscall.s

; int32inc(dest=r0)=int32add16(dest,1) - increment dest
label int32inc
mov r1 1
jmp int32add16 ; this function can return for us

; int32add16(dest=r0, src=r1)
label int32add16
ret

; int32add32(dest=r0, opA=r1)
label int32add32
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Add32
syscall
ret
