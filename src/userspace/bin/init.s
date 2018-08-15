; Simply execs shell

requireend lib/std/proc/exit.s
requireend lib/std/proc/forkexec.s

db shellPath '/bin/sh', 0

; ensure we have pid 0 (otherwise init should already be running)
mov r0 1
syscall
cmp r0 r0 r0
skipeqz r0
jmp error

; call forkexec to start shell
mov r0 shellPath
mov r1 0
mov r2 0
mov r3 0
call forkexec

; error forking?
cmp r0 r0 r0
skipneqz r0
jmp error

; otherwise call waitpid with our own pid so that we sleep forever
label parent
mov r0 6
mov r1 0
mov r2 0 ; infinite timeout
syscall
jmp parent ; in case waitpid fails for any reason

; forkexec error
label error
mov r0 1
call exit

