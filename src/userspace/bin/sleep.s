require lib/sys/sys.s

requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s
requireend lib/std/time/sleep.s

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Grab sleep time in seconds from first argument
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp done

; convert to integer
call strtoint

; call sleep
call sleep

; exit
label done
mov r0 0
call exit
