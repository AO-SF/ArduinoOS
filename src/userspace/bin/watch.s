require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/runpath.s
requireend lib/std/proc/waitpid.s
requireend lib/std/str/strtoint.s
requireend lib/std/time/sleep.s

db usageStr 'usage: watch interval cmd cmdArg\n', 0
db forkErrorStr 'could not fork\n', 0
db emptyStr 0

aw delayTime 1
ab childPid 1
ab quitFlag 1
ab argBuf ArgLenMax

jmp start

; Suicide handler - here as handlers must be in first 256 bytes
label suicideHandler
; Protect regs
push16 r0
push16 r1
push16 r2
; Set quit flag so that after child dies we stop looping
mov r0 quitFlag
mov r1 1
store8 r0 r1
; Check for child
mov r1 childPid
load8 r1 r1
mov r2 PidMax
cmp r2 r1 r2
skipneq r2
jmp suicideHandlerEnd
; Pass suicide signal onto child
mov r0 SyscallIdSignal
mov r2 SignalIdSuicide
syscall
; Restore regs
label suicideHandlerEnd
pop16 r2
pop16 r1
pop16 r0
ret

; Start of execution
label start

; Check argument count (need at least 3 arguments)
mov r0 SyscallIdArgc
syscall
mov r1 3
cmp r0 r0 r1
skipge r0
jmp usage

; Store invalid PID in childPid in case suicide handle is invoked before we fork
mov r0 childPid
mov r1 PidMax
store8 r0 r1

; Set quit flag to false now before we register suicide hander
mov r0 quitFlag
mov r1 0
store8 r0 r1

; Register suicide handler (to exit fast)
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; Grab delay time
mov r0 SyscallIdArgvN
mov r1 1
mov r2 argBuf ; use this as a scratch buffer for now
mov r3 ArgLenMax
syscall

mov r0 argBuf
call strtoint

mov r1 delayTime
store16 r1 r0

; Attempt to run command
label runCommand

; Told to quit by suicide handler?
mov r0 quitFlag
load8 r0 r0
cmp r0 r0 r0
skipeqz r0
jmp done

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
mov r1 2
mov r2 ArgMax ; pass all arguments
syscall

; Exec only returns on error
jmp done

label forkParent
; Store childs PID
mov r1 childPid
store8 r1 r0

; Wait for child to die
call waitpid ; childs PID is in r0 already

; Clear stored child PID
mov r0 childPid
mov r1 PidMax
store8 r0 r1

; Told to quit by suicide handler?
mov r0 quitFlag
load8 r0 r0
cmp r0 r0 r0
skipeqz r0
jmp done

; Wait for given delay
mov r0 delayTime
load16 r0 r0
call sleep

; Loop to run command again
jmp runCommand

; Error forking - print error then exit
label forkError
mov r0 forkErrorStr
call puts0
jmp done

; Usage error - print info then exit
label usage
mov r0 usageStr
call puts0
jmp done

; Exit
label done
mov r0 0
call exit
