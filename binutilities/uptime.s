jmp start

require lib/std/io/fput.s
require lib/std/io/fputtime.s
require lib/std/proc/exit.s
require lib/std/time/timemonotonic.s

label start

call gettimemonotonic
call puttime

mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
