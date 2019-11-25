require ../../sys/syscall.s

; readCount=fread(fd=r0, offset=r1, data=r2, dataLen=r3) - places number of bytes read into r0
label fread
; Use read syscall
mov r4 r3
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdRead
syscall
ret

; readCount=fread32(fd=r0, offsetPtr=r1, data=r2, dataLen=r3 - version of fread with 32 bit offset argument, places number of bytes read into r0
label fread32
; Use read32 syscall
mov r4 r3
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdRead32
syscall
ret
