require lib/sys/sys.s

requireend lib/pin/pinset.s
requireend lib/pin/strtopin.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: setpin pin state\n',0
db badPinStr 'Bad pin\n',0

ab pinArgBuf ArgLenMax
ab stateArgBuf ArgLenMax

; Grab args
mov r0 SyscallIdArgvN
mov r1 1
mov r2 pinArgBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage
mov r0 SyscallIdArgvN
mov r1 2
mov r2 stateArgBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Convert args to integers
mov r0 pinArgBuf
call strtopin

mov r1 PinInvalid
cmp r1 r0 r1
skipneq r1
jmp badPin

push8 r0
mov r0 stateArgBuf
call strtoint
mov r1 r0
pop8 r0

; Use pin library to set state
call pinset

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
