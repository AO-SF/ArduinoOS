requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/getpwd.s
requireend lib/std/str/strlen.s

ab argBuf 64

ab queryDir 64
ab queryDirFd 1
ab queryDirLen 1

; Check for argument, otherwise use pwd
mov r0 3
mov r1 1
mov r2 argBuf
mov r3 64
syscall
cmp r0 r0 r0
skipneqz r0
jmp noArg
mov r0 queryDir
mov r1 argBuf
call getabspath
jmp gotArg

label noArg
mov r0 queryDir
call getpwd
label gotArg

; Attempt to open queryDir
mov r0 258
mov r1 queryDir
syscall

mov r1 queryDirFd
store8 r1 r0

; Check for bad fd
mov r0 queryDirFd
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp error

; Read path back from fd (to handle user passing relative paths for example)
mov r0 261
mov r1 queryDirFd
load8 r1 r1
mov r2 queryDir
syscall

; Find queryDir length
mov r0 queryDir
call strlen
mov r1 queryDirLen
store8 r1 r0

; Loop calling getChildN and printing results
mov r2 0
push r2

label loopStart
mov r0 260
mov r1 queryDirFd
load8 r1 r1
pop r2
mov r3 queryDir
syscall
inc r2
push r2

; End of children?
cmp r0 r0 r0
skipneqz r0
jmp success

; Strip queryDir from front of child path
mov r0 queryDir
mov r1 queryDirLen
load8 r1 r1
add r0 r0 r1

mov r2 1
cmp r2 r1 r2
skiple r2
inc r0

; Print child path followed by a space
call puts0
mov r0 ' '
call putc0

; Loop again to look for another child
jmp loopStart

; Success - terminate list with newline
label success
mov r0 '\n'
call putc0

; Close queryDir
mov r0 259
mov r1 queryDirFd
load8 r1 r1
syscall

; Exit
mov r0 0
call exit

; Error
label error
mov r0 1
call exit
