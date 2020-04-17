require ../../sys/syscall.s

requireend int32set.s

; int32mul1616(dest=r0, opA=r1, opB=r2) - stores product of two 16-bit values into 32 bit dest
label int32mul1616
; Set dest=opA
push16 r0
push16 r2
call int32set16
pop16 r1
pop16 r0
; Compute dest*=opB
jmp int32mul16 ; this function will return for us

; int32mul16(dest=r0, opA=r1) - stores product of 32-bit value dest and 16-bit value opA into dest
label int32mul16
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Mul16
syscall
ret

; int32mul32(dest=r0, opA=r1) - stores product of two 32-bit values dest and opA into dest
label int32mul32
mov r2 r1
mov r1 r0
mov r0 SyscallIdInt32Mul32
syscall
ret
