jmp start

require libio.s
require libproc.s
require libstr.s

ab pathBuf 64
ab pwdFd 1

label start

; Grab pwd
mov r0 pathBuf
call getpwd

; Attempt to open pwd
mov r0 258
mov r1 pathBuf
syscall

mov r1 pwdFd
store8 r1 r0

; Check for bad fd
mov r0 pwdFd
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp error

; Loop calling getChildN and printing results
mov r2 0
push r2

label loopStart
mov r0 260
mov r1 pwdFd
load8 r1 r1
pop r2
mov r3 pathBuf
syscall
inc r2
push r2

; End of children?
cmp r0 r0 r0
skipneqz r0
jmp success

; Print child path followed by a space
mov r0 pathBuf
call puts
mov r0 ' '
call putc

; Loop again to look for another child
jmp loopStart

; Success - terminate list with newline
label success
mov r0 '\n'
call putc

; Close pwd
mov r0 259
mov r1 pwdFd
load8 r1 r1
syscall

; Exit
mov r0 0
call exit

; Error
label error
mov r0 1
call exit
