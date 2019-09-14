require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: id mountPoint\n',0

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
push8 r0

; Grab mount point arg
mov r0 SyscallIdArgvN
mov r1 2
mov r2 argBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage ; id is not popped from stack but no harm

; Use syscall to mount
mov r0 SyscallIdHwDeviceSdCardReaderMount
pop8 r1
mov r2 argBuf
syscall

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit
