require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strequal.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: id type\n',0
db badTypeStr 'Bad type\n',0
db typeStrRaw 'raw',0
db typeStrSdCardReader 'sdcardreader',0
db typeStrDht22 'dht22',0

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

; Grab type arg
mov r0 SyscallIdArgvN
mov r1 2
mov r2 argBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage ; id is not popped from stack but no harm

; Convert type arg to integer
mov r0 argBuf
mov r1 typeStrRaw
call strequal
cmp r0 r0 r0
skipeqz r0
jmp typeIsRaw

mov r0 argBuf
mov r1 typeStrSdCardReader
call strequal
cmp r0 r0 r0
skipeqz r0
jmp typeIsSdCardReader

mov r0 argBuf
mov r1 typeStrDht22
call strequal
cmp r0 r0 r0
skipeqz r0
jmp typeIsDht22

jmp badType

label typeIsRaw
mov r2 SyscallHwDeviceTypeRaw
jmp doSyscall

label typeIsSdCardReader
mov r2 SyscallHwDeviceTypeSdCardReader
jmp doSyscall

label typeIsDht22
mov r2 SyscallHwDeviceTypeDht22
jmp doSyscall

; Use syscall to register device
label doSyscall
pop8 r1 ; pop desired id/slot off stack
mov r0 SyscallIdHwDeviceRegister
syscall

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit

label badType
mov r0 badTypeStr
call puts0
mov r0 1
call exit
