jmp start

require stdlib/libiofput.s
require stdlib/libprocexit.s
require stdlib/libprocgetpwd.s
require stdlib/libstrstrlen.s

ab pathBuf 64
ab pwdFd 1
ab pwdLen 1

label start

; Grab pwd and find length
mov r0 pathBuf
call getpwd

mov r0 pathBuf
call strlen
mov r1 pwdLen
store8 r1 r0

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

; Strip pwd from front of child path
mov r0 pathBuf
mov r1 pwdLen
load8 r1 r1
add r0 r0 r1

mov r2 1
cmp r2 r1 r2
skiple r2
inc r0

; Print child path followed by a space
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
