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

ab cmdBuf ArgLenMax
ab cmdArg1Buf ArgLenMax
aw delayTime 1

; Check argument count (need at least 3 arguments)
mov r0 SyscallIdArgc
syscall
mov r1 3
cmp r0 r0 r1
skipge r0
jmp usage

; Grab delay time
mov r0 SyscallIdArgvN
mov r1 1
mov r2 cmdBuf ; use this as a scratch buffer for now
mov r3 ArgLenMax
syscall

mov r0 cmdBuf
call strtoint

mov r1 delayTime
store16 r1 r0

; Grab command and argument (we can only have 4 arguments ourselves and use 2, and the command itself needs one, so this only leaves a single argument to pass onto the command)
mov r0 SyscallIdArgvN
mov r1 2
mov r2 cmdBuf
mov r3 ArgLenMax
syscall

mov r0 SyscallIdArgvN
mov r1 3
mov r2 cmdArg1Buf
mov r3 ArgLenMax
syscall

; Attempt to run command
label runCommand

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
; Exec given argument
mov r0 cmdBuf
mov r1 cmdArg1Buf
mov r2 emptyStr
mov r3 emptyStr
call runpath

; Exec only returns on error
jmp done

label forkParent
; Wait for child to die
call waitpid ; childs PID is in r0 already

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
