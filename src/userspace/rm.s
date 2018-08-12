requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s

db usageStr 'usage: rm PATH\n', 0

ab pathArg 64
ab rmScratchBuf 64

; Get path argument
mov r0 3
mov r1 1
mov r2 rmScratchBuf
mov r3 64
syscall
cmp r0 r0 r0
skipneqz r0
jmp showUsage

; Call getpath
mov r0 pathArg
mov r1 rmScratchBuf
call getpath

; Invoke delete syscall
mov r0 267
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
