requireend pinopen.s
requireend pinsetmode.s

label pinset ; num=r0, value=r1, sets pin to value
; store value on stack
push8 r1
; open pin device file
call pinopen
push8 r0 ; save fd on stack
; set to output
mov r1 PinModeOutput
call pinsetmode
; write byte
mov r0 SyscallIdWrite
pop8 r1 ; pop fd off stack
mov r2 0
dec r6 ; use value on stack
mov r3 r6
mov r4 1
syscall
; close pin device file
mov r0 SyscallIdClose
syscall
ret
