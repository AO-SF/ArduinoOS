jmp start

require lib/std/io/fget.s
require lib/std/io/fput.s
require lib/std/proc/exit.s
require lib/std/proc/getabspath.s

ab argBuf 64
ab pathBuf 64
ab fd 1

label start

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push r0
call catArgN ; this will exit for us if no such argument
pop r0
inc r0
jmp loopstart

; Exit
mov r0 0
call exit

label error
mov r0 1
call exit

label catArgN

; Get arg
mov r1 r0
mov r0 3
mov r2 argBuf
mov r3 64
syscall

; No arg found?
cmp r0 r0 r0
skipneqz r0
jmp exit

; Convert to absolute path
mov r0 pathBuf
mov r1 argBuf
call getabspath

; Open file
mov r0 258
mov r1 pathBuf
syscall

mov r1 fd
store8 r1 r0

; Check for bad fd
cmp r1 r0 r0
skipneqz r1
jmp error

; Read data from file, printing to stdout
mov r1 0 ; loop index
label catArgNLoopStart

; Read character
mov r0 fd
load8 r0 r0
push r1
call fgetc
pop r1

; Check for EOF
mov r2 256
cmp r2 r0 r2
skipneq r2
jmp catArgNLoopEnd

; Print character
push r1
call putc0
pop r1

; Advance to next character
inc r1
jmp catArgNLoopStart
label catArgNLoopEnd

; Close file
mov r0 259
mov r1 fd
load8 r1 r1
syscall

ret
