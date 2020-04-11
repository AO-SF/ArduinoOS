requireend pinopen.s
requireend pinsetmode.s

label pinget ; num=r0, returns boolean on/off result in r0
; open pin device file
call pinopen
cmp r1 r0 r0
skipneqz r1
jmp pingetError
push8 r0
; set to input
mov r1 PinModeInput
call pinsetmode
; read byte
mov r0 SyscallIdRead
pop8 r1
mov r2 0
mov r3 r6 ; use stack to store character
mov r4 1
syscall
mov r2 r0
; close pin device file
mov r0 SyscallIdClose
syscall
; bad read?
cmp r2 r2 r2
skipneqz r2
jmp pingetError
; return read byte
load8 r0 r6
ret
; error case
label pingetError
mov r0 0
ret
