jmp start

require lib/std/io/fput.s
require lib/std/io/fputdec.s
require lib/std/proc/exit.s

db header '  PID COMMAND\n', 0

ab psPidPid 1
ab psPidCommandBuf 64

label start

; Print header
mov r0 header
call puts0

; Loop over pids
mov r0 0
label loopstart

; Hit max pid?
mov r1 64
cmp r1 r0 r1
skiplt r1
jmp loopend

; Call psPid to get and print data for this process (if any)
push r0
call psPid
pop r0

; Try next pid
inc r0
jmp loopstart
label loopend

; Exit
mov r0 0
call exit

label psPid

; Store pid
mov r1 psPidPid
store8 r1 r0

; Grab command
mov r1 r0
mov r0 7
mov r2 psPidCommandBuf
syscall

cmp r0 r0 r0
skipeqz r0
jmp psPidExists
ret
label psPidExists

; Print pid
mov r0 psPidPid
load8 r0 r0
call putdecpad
mov r0 ' '
call putc0

; Print command
mov r0 psPidCommandBuf
call puts0

; Terminate line
mov r0 '\n'
call putc0

ret
