require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s

db usageStr 'usage: rm PATH\n', 0

ab pathArg PathMax

; Get path argument
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp showUsage

; Call getpath
mov r1 r0
mov r0 pathArg
call getpath

; Invoke delete syscall
mov r0 SyscallIdDelete
mov r1 pathArg
syscall

; Exit
mov r0 0
call exit
; Errors
label showUsage
mov r0 usageStr
call puts0
mov r0 1
call exit
