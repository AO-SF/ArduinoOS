require ../../sys/syscall.s

; r0=strcmp(p1Addr=r0, p2Addr=r1)
label strcmp
; Simply use syscall for speed
mov r2 r1
mov r1 r0
mov r0 SyscallIdStrCmp
syscall
ret
