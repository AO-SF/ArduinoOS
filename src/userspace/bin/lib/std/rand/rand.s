require ../../sys/sys.s

requireend ../io/fget.s
requireend ../math/mod.s

db rand8DevURandomPath '/dev/urandom',0

label rand8 ; returns random 8-bit value in r0
; Open /dev/urandom
mov r0 SyscallIdOpen
mov r1 rand8DevURandomPath
mov r2 FdModeRO
syscall
cmp r1 r0 r0
skipneqz r1
jmp rand8BadOpen
; Read a single byte (8 bits)
push8 r0
mov r1 0
call fgetc
mov r2 r0
; Close file
mov r0 SyscallIdClose
pop8 r1
syscall
; Move read value into r0 to return
; Also AND it with 255 in case fgetc returned EOF
mov r0 r2
mov r1 255
and r0 r0 r1
ret
; Error case
label rand8BadOpen
mov r0 0
ret

label rand16 ; returns random 16-bit value in r0
; Call rand8 twice to get two 8 bit values
call rand8
push8 r0
call rand8
pop8 r1
; Combine r0 and r1 to make single 16 bit value
mov r2 8
shl r0 r0 r2
or r0 r0 r1
ret

label randN ; (r0=n) - returns random integer in interval [0,n)
; Generate a 16 bit number
push16 r0
call rand16
pop16 r1
; Use mod to reduce random value down to correct range
call mod
ret
