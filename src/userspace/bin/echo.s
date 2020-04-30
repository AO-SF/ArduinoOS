require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Init ready for loop
mov r1 1

label loopstart

; Print space if needed
mov r0 1
cmp r2 r1 r0
mov r0 ' '
push8 r1
skiple r2
call putc0
pop8 r1

; Get arg
mov r0 SyscallIdArgvN
syscall

cmp r2 r0 r0
skipneqz r2
jmp loopend

; Write out arg
push8 r1
call puts0
pop8 r1

; Move onto next argument
inc r1
jmp loopstart

label loopend

; Print newline
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
