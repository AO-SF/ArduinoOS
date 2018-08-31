requireend pinopen.s
requireend pinsetmode.s

ab pingetScratchByte 1

label pinget ; num=r0, returns boolean on/off result in r0
; open pin device file
call pinopen
push8 r0
; set to input
mov r1 PinModeInput
call pinsetmode
; read byte
; TODO: Check number of bytes read is 1
mov r0 SyscallIdRead
pop8 r1
mov r2 0
mov r3 pingetScratchByte
mov r4 1
syscall
; close pin device file
mov r0 SyscallIdClose
syscall
; return read byte
mov r0 pingetScratchByte
load8 r0 r0
ret
