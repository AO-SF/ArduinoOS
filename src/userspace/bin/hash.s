require lib/sys/sys.s

requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/io/fputhex.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s

ab argBuf PathMax
ab pathBuf PathMax
ab fd 1
aw hash 1

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push8 r0
call hashArgN ; this will exit for us if no such argument
pop8 r0
inc r0
jmp loopstart

; Exit
label done
mov r0 0
call exit

label error
mov r0 1
call exit

label hashArgN

; Get arg
mov r1 r0
mov r0 SyscallIdArgvN
mov r2 argBuf
mov r3 PathMax
syscall

; No arg found?
cmp r0 r0 r0
skipneqz r0
jmp done

; Convert to absolute path
mov r0 pathBuf
mov r1 argBuf
call getabspath

; Open file
mov r0 SyscallIdOpen
mov r1 pathBuf
mov r2 FdModeRO
syscall

mov r1 fd
store8 r1 r0

; Check for bad fd
cmp r1 r0 r0
skipneqz r1
jmp error

; Read data from file, printing to stdout
mov r0 hash
mov r1 5381
store16 r0 r1

mov r1 0 ; loop index
label hashArgNLoopStart

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
jmp hashArgNLoopEnd

; Add character to hash
mov r4 hash
load16 r2 r4
mov r3 5
shl r2 r2 r3
add r2 r2 r0
load16 r3 r4
add r2 r2 r3
store16 r4 r2

; Advance to next character
inc r1
jmp hashArgNLoopStart
label hashArgNLoopEnd

; Print hash in hex
mov r0 hash
load16 r0 r0
call puthex16
mov r0 '\n'
call putc0

; Close file
mov r0 SyscallIdClose
mov r1 fd
load8 r1 r1
syscall

ret
