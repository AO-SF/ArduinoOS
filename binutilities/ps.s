jmp start

require lib/std/io/fput.s
require lib/std/io/fputdec.s
require lib/std/math/int32.s
require lib/std/proc/exit.s
require lib/std/str/strpad.s

db header '  PID  %CPU    STATE COMMAND\n', 0
aw cpuCounts 64
aw cpuTotal 1

ab psPidPid 1
ab psPidStateBuf 64
ab psPidCommandBuf 64
ab psPidScratchBuf 64

ab psPidInt32 4

label start

; Print header
mov r0 header
call puts0

; Grab all cpu counts now in one go and compute sum
; This at least makes sure the percentages add up properly
; Although there is still an (unavoidable) race condition between this and actually outputting the data
mov r0 9
mov r1 cpuCounts
syscall

mov r0 cpuTotal
mov r1 0
store16 r0 r1

mov r1 0
mov r2 cpuCounts
mov r3 0
label cpusumstart
mov r4 64
cmp r4 r3 r4
skipneq r4
jmp cpusumend
load16 r4 r2
add r1 r1 r4
inc2 r2
inc r3
jmp cpusumstart
label cpusumend

mov r0 cpuTotal
store16 r0 r1

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
mov r0 7
mov r1 psPidPid
load8 r1 r1
mov r2 psPidCommandBuf
syscall

cmp r0 r0 r0
skipeqz r0
jmp psPidGotCommand
ret
label psPidGotCommand

; Grab state
mov r0 8
mov r1 psPidPid
load8 r1 r1
mov r2 psPidStateBuf
syscall

cmp r0 r0 r0
skipeqz r0
jmp psPidGotState
ret
label psPidGotState

; Print pid
mov r0 psPidPid
load8 r0 r0
call putdecpad
mov r0 ' '
call putc0

; Print cpu load
mov r0 psPidPid
load8 r0 r0
mov r1 2
mul r0 r0 r1
mov r1 cpuCounts
add r0 r0 r1
load16 r0 r0

mov r1 r0
mov r0 psPidInt32
mov r2 100
call int32mul1616

mov r0 psPidInt32
mov r1 cpuTotal
load16 r1 r1
call int32div16

mov r0 psPidInt32
call int32get16
call putdecpad
mov r0 ' '
call putc0

; Print state (padding first)
mov r0 psPidScratchBuf
mov r1 psPidStateBuf
mov r2 8
call strpadfront

mov r0 psPidScratchBuf
call puts0
mov r0 ' '
call putc0

; Print command
mov r0 psPidCommandBuf
call puts0

; Terminate line
mov r0 '\n'
call putc0

ret
