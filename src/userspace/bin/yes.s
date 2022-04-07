require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/str/strcpy.s

db standardMsg 'y', 0

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Grab argument
mov r0 SyscallIdArgvN
mov r1 1
syscall

; If no argument, use standard one instead
cmp r1 r0 r0
skipneqz r1
mov r0 standardMsg

; Print argument repeatedly (r0 contains ptr to string)
label loopstart
push16 r0
call puts0
mov r0 '\n'
call putc0
pop16 r0
jmp loopstart

