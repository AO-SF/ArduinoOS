; Simply execs shell

db execPath '/bin/sh', 0

; call exec
mov r0 5
mov r1 execPath
mov r2 0 ; no argument
syscall

; exec only returns on failure
mov r0 0
mov r1 1
syscall
