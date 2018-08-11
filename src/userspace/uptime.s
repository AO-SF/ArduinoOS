requireend lib/std/io/fput.s
requireend lib/std/io/fputtime.s
requireend lib/std/proc/exit.s
requireend lib/std/time/timemonotonic.s

; Get time since booting and print it, followed by a newline
call gettimemonotonic
call puttime
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
