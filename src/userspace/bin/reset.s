requireend lib/curses/curses.s
requireend lib/std/proc/exit.s

call cursesCursorShow
mov r0 1
call cursesSetEcho
call cursesReset

mov r0 0
call exit
