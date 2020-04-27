require lib/sys/sys.s

requireend lib/pin/pinset.s
requireend lib/pin/pinvalid.s
requireend lib/pin/strtopin.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: setpin pin state\n',0
db badPinStr 'Bad pin\n',0

; Grab pin arg and convert to integer
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

call strtopin
push8 r0 ; protect pinNum

call pinvalid
cmp r1 r0 r0
skipneqz r1
jmp badPin ; no need to restore stack as we will exit very soon

; Grab state arg and convert to integer
mov r0 SyscallIdArgvN
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

call strtoint

mov r1 r0 ; place state into r1
pop8 r0 ; restore pinNum into r0

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
