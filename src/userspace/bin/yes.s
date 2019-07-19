require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/str/strcpy.s

db standardMsg 'y', 0
ab argBuf ArgLenMax

; Grab argument
mov r0 SyscallIdArgvN
mov r1 1
mov r2 argBuf
mov r3 ArgLenMax
syscall

; If no argument, copy standard one into buffer instead
cmp r0 r0 r0
skipeqz r0
jmp loopstart

mov r0 argBuf
mov r1 standardMsg
call strcpy

; Print argument repeatedly
label loopstart
mov r0 argBuf
call puts0
mov r0 '\n'
call putc0
jmp loopstart

