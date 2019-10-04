require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/io/fputtime.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/runpath.s
requireend lib/std/proc/waitpid.s
requireend lib/std/time/timemonotonic.s

db preMsg 'took: ', 0
db forkErrorStr 'could not fork\n', 0
db emptyStr 0

aw startTime 1
ab childPid 1

jmp start

; Suicide handler - here as handlers must be in first 256 bytes
label suicideHandler
; No child?
mov r0 childPid
load8 r0 r0
mov r1 PidMax
cmp r1 r0 r1
skipneq r1
ret
; Child running - pass on suicide signal
mov r0 SyscallIdSignal
mov r2 SignalIdSuicide
syscall
ret

; Program start
label start

; Set childs pid to invalid in case suicide handler fires before we fork
mov r0 PidMax
mov r1 childPid
store8 r1 r0

; Register suicide handler (to exit fast)
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; Get start time and store into variable
call gettimemonotonic

mov r1 startTime
store16 r1 r0

; Call fork
mov r0 SyscallIdFork
syscall

mov r1 PidMax
cmp r1 r0 r1
skipneqz r1
jmp forkChild
skipneq r1
jmp forkError
jmp forkParent

label forkChild
; Exec using exec2 to pass on our own argv arguments
mov r0 SyscallIdExec2
mov r1 1
mov r2 ArgMax ; pass all arguments
syscall

; Exec only returns on error
jmp done

label forkParent
; Store child PID
mov r1 childPid
store8 r1 r0

; Wait for child to die
call waitpid ; childs PID is in r0 already
jmp childFinished

label forkError
; Print error
mov r0 forkErrorStr
call puts0
jmp done

label childFinished
; Record endTime
call gettimemonotonic

; Print time delta
mov r1 startTime
load16 r1 r1
sub r0 r0 r1

push16 r0
mov r0 preMsg
call puts0
pop16 r0
call puttime
mov r0 '\n'
call putc0

; Exit
label done
mov r0 0
call exit
