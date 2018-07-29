jmp start

require lib/std/io/fput.s
require lib/std/str/strcpy.s

db standardMsg 'y', 0

ab argBuf 64

label start

; Grab argument
mov r0 3
mov r1 1
mov r2 argBuf
mov r3 64
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

