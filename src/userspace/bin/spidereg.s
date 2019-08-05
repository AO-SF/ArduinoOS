require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: id\n',0

ab argBuf ArgLenMax

; Grab id arg
mov r0 SyscallIdArgvN
mov r1 1
mov r2 argBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Convert id arg to integer
mov r0 argBuf
call strtoint

; Use syscall to deregister device
mov r1 r0
mov r0 SyscallIdSpiDeviceDeregister
syscall

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit
