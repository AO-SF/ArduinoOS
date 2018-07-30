; Simply execs shell

requireend lib/std/proc/exit.s

db execPath '/bin/sh', 0

; call exec (no arguments)
mov r0 5
mov r1 execPath
mov r2 0
mov r3 0
mov r4 0
syscall

; exec only returns on failure
mov r0 1
call exit
