jmp start

require lib/std/io/fput.s
require lib/std/io/fputdec.s
require lib/std/proc/exit.s
require lib/std/proc/getabspath.s

db preMsg 'took: ', 0
db postMsg 's\n', 0
db forkErrorStr 'could not fork\n', 0

aw startTime 1
ab argBuf 64
ab cmdBuf 64

label start

; Grab 1st argument
mov r0 3
mov r1 1
mov r2 argBuf
mov r3 64
syscall

; Ensure path is absolute
mov r0 cmdBuf
mov r1 argBuf
call getabspath

; Get start time and store into variable
mov r0 768
syscall

mov r1 startTime
store16 r1 r0

; Call fork
mov r0 4
syscall

mov r1 64
cmp r1 r0 r1
skipneqz r1
jmp forkChild
skipneq r1
jmp forkError
jmp forkParent

label forkChild
; Exec given argument
mov r0 5
mov r1 cmdBuf
mov r2 0
syscall

; Exec only returns on error
jmp done

label forkParent
; Wait for child to die
mov r1 r0
mov r0 6
syscall

jmp childFinished

label forkError
; Print error
mov r0 forkErrorStr
call puts0
jmp done

label childFinished
; Record endTime
mov r0 768
syscall

; Print time delta
mov r1 startTime
load16 r1 r1
sub r0 r0 r1

push r0
mov r0 preMsg
call puts0
pop r0
call putdec
mov r0 postMsg
call puts0

; Exit
label done
mov r0 0
call exit
