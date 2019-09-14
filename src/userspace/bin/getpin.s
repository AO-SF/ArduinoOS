require lib/sys/sys.s

requireend lib/pin/pinget.s
requireend lib/pin/strtopin.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s

db usageStr 'usage: getpin pin\n',0
db badPinStr 'Bad pin\n',0
db onStr 'on\n',0
db offStr 'off\n',0

ab argBuf ArgLenMax

; Grab arg
mov r0 SyscallIdArgvN
mov r1 1
mov r2 argBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Convert arg to integer pin num
mov r0 argBuf
call strtopin

; Bad pin?
mov r1 PinInvalid
cmp r1 r0 r1
skipneq r1
jmp badPin

; Use pin library to get current state
call pinget

; Print state
cmp r1 r0 r0
mov r0 onStr
skipneqz r1
mov r0 offStr
call puts0

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit

label badPin
mov r0 badPinStr
call puts0
mov r0 1
call exit
