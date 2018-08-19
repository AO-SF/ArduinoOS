; init - run startup file followed by a shell, while continuing to run indefintely with pid 0

requireend lib/std/proc/exit.s
requireend lib/std/proc/forkexec.s
requireend lib/std/proc/forkexecwait.s

db startupPath '/etc/startup', 0
db shellPath '/bin/sh', 0

; ensure we have pid 0 (otherwise init should already be running)
mov r0 1
syscall
cmp r0 r0 r0
skipeqz r0
jmp error

; Register suicide signal handler
mov r0 1024
mov r1 3 ; suicide signal id
mov r2 suicideHandler
syscall

; call forkexecwait to run startup file and wait for it to complete
mov r0 startupPath
mov r1 0
mov r2 0
mov r3 0
call forkexecwait

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

label suicideHandler
; For now simply exit
mov r0 0
call exit
