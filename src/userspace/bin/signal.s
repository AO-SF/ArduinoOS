require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: signal pid signalid',0

; Grab pid from first argument and convert to integer
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

call strtoint

mov r3 r0

; Grab signalid from second argument and convert to integer
mov r0 SyscallIdArgvN
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

call strtoint

; Invoke syscall to send signal
mov r2 r0 ; set signal id as 2nd arg
mov r1 r3 ; set process pid as 1st arg
mov r0 SyscallIdSignal
syscall

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit
