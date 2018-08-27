require lib/sys/sys.s

requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s

ab argBuf PathMax
ab pathBuf PathMax
ab fd 1

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push8 r0
call catArgN ; this will exit for us if no such argument
pop8 r0
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
mov r3 PathMax
syscall

; No arg found?
cmp r0 r0 r0
skipneqz r0
jmp error

; Convert to absolute path
mov r0 pathBuf
mov r1 argBuf
call getabspath

; Open file
mov r0 SyscallIdOpen
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
push16 r1
call fgetc
pop16 r1

; Check for EOF
mov r2 256
cmp r2 r0 r2
skipneq r2
jmp catArgNLoopEnd

; Print character
push16 r1
call putc0
pop16 r1

; Advance to next character
inc r1
jmp catArgNLoopStart
label catArgNLoopEnd

; Close file
mov r0 SyscallIdClose
mov r1 fd
load8 r1 r1
syscall

ret
