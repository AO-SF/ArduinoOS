requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s
requireend lib/std/time/sleep.s

ab argBuf 64

; grab first argument
mov r0 3
mov r1 1
mov r2 argBuf
mov r3 64
syscall

; convert to integer
mov r0 argBuf
call strtoint

; call sleep
call sleep

; exit
mov r0 0
call exit
