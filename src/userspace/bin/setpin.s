requireend lib/pin/pinset.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: setpin pinnum state\n',0

ab pinNumArgBuf 64
ab stateArgBuf 64

; Grab args
mov r0 3
mov r1 1
mov r2 pinNumArgBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage
mov r0 3
mov r1 2
mov r2 stateArgBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Convert args to integers
mov r0 pinNumArgBuf
call strtoint
push r0
mov r0 stateArgBuf
call strtoint
mov r1 r0
pop r0

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
