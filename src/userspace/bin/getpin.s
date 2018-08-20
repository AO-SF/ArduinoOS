requireend lib/pin/pinget.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: getpin pinnum\n',0
db onStr 'on\n',0
db offStr 'off\n',0

ab argBuf 64

; Grab arg
mov r0 3
mov r1 1
mov r2 argBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Convert arg to integer pin num
mov r0 argBuf
call strtoint

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
