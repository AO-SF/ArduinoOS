; Simply execs shell

requireend lib/std/proc/exit.s

db execPath '/bin/sh', 0

; ensure we have pid 0 (otherwise init should already be running)
mov r0 1
syscall
cmp r0 r0 r0
skipeqz r0
jmp error

; call fork
mov r0 4
syscall

mov r1 64 ; PidMax
cmp r1 r0 r1
skipneq r1
jmp error

cmp r1 r0 r0
skipneqz r1
jmp child
jmp parent

; call waitpid with our own pid so that we sleep forever
label parent
mov r0 6
mov r1 0
syscall
jmp parent ; in case waitpid fails for any reason

; call exec (no arguments)
label child
mov r0 5
mov r1 execPath
mov r2 0
mov r3 0
mov r4 0
syscall

; exec only returns on failure
label error
mov r0 1
call exit
