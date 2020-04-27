require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: id\n',0

; Grab id arg
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

; Convert id arg to integer
call strtoint

; Use syscall to deregister device
mov r1 r0
mov r0 SyscallIdHwDeviceDeregister
syscall

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit
