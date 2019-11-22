require lib/sys/sys.s

requireend lib/std/int32/int32sub.s
requireend lib/std/int32/int32time.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/runpath.s
requireend lib/std/proc/waitpid.s

db preMsg 'took: ', 0
db forkErrorStr 'could not fork\n', 0
db emptyStr 0

aw startTime 2 ; 32 bit value in seconds
aw deltaTime 2
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
mov r0 startTime
call int32gettimemonotonic

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
mov r0 deltaTime
call int32gettimemonotonic

; Calculate and print time delta
mov r0 deltaTime
mov r1 startTime
call int32sub32

mov r0 preMsg
call puts0
mov r0 deltaTime
call int32puttime
mov r0 '\n'
call putc0

; Exit
label done
mov r0 0
call exit
