requireend pinopen.s

ab pinsetScratchByte 1

label pinset ; num=r0, value=r1, sets pin to value
; store value in scratch byte
mov r2 pinsetScratchByte
store8 r2 r1
; open pin device file
call pinopen
mov r1 r0
; write byte
mov r0 SyscallIdWrite
mov r2 0
mov r3 pinsetScratchByte
mov r4 1
syscall
; close pin device file
mov r0 SyscallIdClose
syscall
ret
