require lib/sys/sys.s

requireend lib/std/proc/exit.s

; Register suicide signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; Simply loop forever
label loop
jmp loop

; Suicide handler to exit quickly
label suicideHandler
mov r0 0
call exit
