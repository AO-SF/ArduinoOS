require lib/sys/sys.s

requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s
requireend lib/std/time/sleep.s

ab argBuf ArgLenMax

; Register suicide signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; grab first argument
mov r0 SyscallIdArgvN
mov r1 1
mov r2 argBuf
mov r3 ArgLenMax
syscall

; convert to integer
mov r0 argBuf
call strtoint

; call sleep
call sleep

; exit
label suicideHandler
mov r0 0
call exit
