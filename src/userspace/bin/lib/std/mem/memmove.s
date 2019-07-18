; memmove(destAddr=r0, srcAddr=r1, size=r2)
label memmove
; simply use syscall
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 1538
syscall
ret
