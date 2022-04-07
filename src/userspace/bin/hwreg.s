require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strcmp.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: id type\n',0
db badTypeStr 'Bad type\n',0
db typeStrKeypad 'keypad',0
db typeStrSdCardReader 'sdcardreader',0
db typeStrDht22 'dht22',0

ab pins HwDevicePinMax

; Grab id arg
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

; Convert id arg to integer
call strtoint
push8 r0

; Grab type arg
mov r0 SyscallIdArgvN
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage ; id is not popped from stack but no harm

; Convert type arg to integer
push16 r0
mov r1 typeStrKeypad
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp typeIsKeypad

push16 r0
mov r1 typeStrSdCardReader
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp typeIsSdCardReader

push16 r0
mov r1 typeStrDht22
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp typeIsDht22

jmp badType

label typeIsKeypad
mov r0 SyscallHwDeviceTypeKeypad
push8 r0
jmp foundType

label typeIsSdCardReader
mov r0 SyscallHwDeviceTypeSdCardReader
push8 r0
jmp foundType

label typeIsDht22
mov r0 SyscallHwDeviceTypeDht22
push8 r0
jmp foundType

; Found type - now read pins and store in array
label foundType
mov r1 3 ; arg number
mov r2 pins ; pointer to current entry in pins array
label pinLoopStart

; Ensure we do not write beyond the pins array in the case that extra arguments are passed
mov r3 HwDevicePinMax
inc3 r3
cmp r3 r1 r3
skiplt r3
jmp pinLoopEnd

; Read pin from arg
mov r0 SyscallIdArgvN
syscall
cmp r3 r0 r0
skipneqz r3
jmp pinLoopEnd

; Convert to int
push8 r1
push16 r2
call strtoint
pop16 r2
pop8 r1

; Store in pins array
store8 r2 r0

; Advance to next arg
inc r1
inc r2
jmp pinLoopStart
label pinLoopEnd

; Use syscall to register device
mov r0 SyscallIdHwDeviceRegister
pop8 r2 ; pop type id off stack
pop8 r1 ; pop desired id/slot off stack
mov r3 pins
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
