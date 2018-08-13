requireend pinopen.s

ab pingetScratchByte 1

label pinget ; num=r0, returns boolean on/off result in r0
; open pin device file
call pinopen
mov r1 r0
; read byte
; TODO: Check number of bytes read is 1
mov r0 256
mov r2 0
mov r3 pingetScratchByte
mov r4 1
syscall
; close pin device file
mov r0 259
syscall
; return read byte
mov r0 pingetScratchByte
load8 r0 r0
ret
