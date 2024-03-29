require lib/sys/sys.s

requireend lib/std/int32/int32add.s
requireend lib/std/int32/int32set.s
requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/getpwd.s
requireend lib/std/proc/pathnormalise.s
requireend lib/std/proc/openpath.s
requireend lib/std/proc/runpath.s
requireend lib/std/proc/thread.s
requireend lib/std/proc/waitpid.s
requireend lib/std/str/strchr.s
requireend lib/std/str/strrchr.s
requireend lib/std/str/strcmp.s
requireend lib/std/str/strtrimnewline.s

db prompt '$ ', 0
db forkErrorStr 'could not fork\n', 0
db execErrorStr 'could not exec: ', 0
db dirNotFoundErrorStr 'no such directory: ', 0
db cdStr 'cd', 0
db exitStr 'exit', 0
db emptyStr 0
db homeDir '/home', 0

const inputBufLen 128
ab inputBuf inputBufLen
ab absBuf PathMax
ab pwdBuf PathMax
ab argc 1

ab interactiveMode 1
ab inputFd 1
aw readOffset 2 ; 32 bit int

ab runInBackground 1

ab childPid 1

ab suicideHandlerLock 1
ab interruptHandlerLock 1

; signal handler labels must be within first 256 bytes of executable, so put these 'trampoline' functions first
jmp start
label suicideHandlerTrampoline
jmp suicideHandler
label interruptHandlerTrampoline
jmp interruptHandler
label start

; Setup signal handler locks
mov r1 0
mov r0 suicideHandlerLock
store8 r0 r1
mov r0 interruptHandlerLock
store8 r0 r1

; Clear child PID
mov r0 childPid
mov r1 PidMax
store8 r0 r1

; Register suicide signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandlerTrampoline
syscall

; Register interrupt signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdInterrupt
mov r2 interruptHandlerTrampoline
syscall

; Check for scripts passed as arguments
mov r1 1 ; child loop index
label argLoopStart
push8 r1
mov r0 SyscallIdArgvN
syscall

; No argument?
cmp r1 r0 r0
skipneqz r1
jmp argLoopEnd

; Open file
mov r1 FdModeRO
call openpath

mov r1 inputFd
store8 r1 r0

; Run script
mov r0 0
mov r1 interactiveMode
store8 r1 r0
call shellRunFd

push8 r0 ; save return value

; Close file
mov r0 SyscallIdClose
mov r1 inputFd
load8 r1 r1
syscall

; shellRunFd return indicates we should exit?
pop8 r0
cmp r0 r0 r0
skipneqz r0
jmp finish

; Advance to next arg
pop8 r1
inc r1
jmp argLoopStart
label argLoopEnd
pop8 r1

; Call shellRunFd on stdin fd
mov r0 FdStdin
mov r1 inputFd
store8 r1 r0

mov r0 1
mov r1 interactiveMode
store8 r1 r0

call shellRunFd

label finish
; Exit (success)
mov r0 0
call exit

; Exit (failure)
label exiterror
mov r0 1
call exit

label shellRunFd
mov r0 readOffset
mov r1 0
call int32set16
label shellRunFdInputLoopStart

; Only print prompt in interactive mode
mov r0 interactiveMode
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp shellRunFdInputPromptEnd

; Print pwd (reuse inputBuf to save space)
call getpwd
call puts0

; Print prompt
mov r0 prompt
call puts0
label shellRunFdInputPromptEnd

; Read input
mov r0 inputFd
load8 r0 r0
mov r1 readOffset
mov r2 inputBuf
mov r3 inputBufLen
call fgets32

; Update read offset for next time
push16 r0
mov r1 r0
mov r0 readOffset
call int32add16
pop16 r0

; If empty read then EOF
cmp r0 r0 r0
skipeqz r0
jmp shellRunFdInputNoEof
mov r0 1 ; continue onto next input file
ret
label shellRunFdInputNoEof

; Parse input - clear argc
mov r0 argc
mov r1 1 ; initially set to 1 as we always require a command (so can avoid an increment operation)
store8 r0 r1

; Parse input - trim trailing newline (if any)
mov r0 inputBuf
call strtrimnewline

; Trim trailing comment (if any)
mov r0 inputBuf
mov r1 '#'
call strrchr
cmp r1 r0 r0
skipneqz r1
jmp shellRunFdInputCommentRemoved
mov r1 0
store8 r0 r1
label shellRunFdInputCommentRemoved

; Check for an empty line
mov r0 inputBuf
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp shellRunFdInputLoopStart

; Parse input - check for trailing '&' to indicate background
mov r0 runInBackground
mov r1 0
store8 r0 r1

mov r0 inputBuf
call strlen
cmp r1 r0 r0
skipneqz r1
jmp shellRunFdInputBackgroundCheckEnd
mov r1 inputBuf
add r0 r0 r1
dec r0
load8 r3 r0

mov r1 '&'
cmp r2 r3 r1
skipeq r2
jmp shellRunFdInputBackgroundCheckEnd
mov r1 0 ; remove & from inputBuf
store8 r0 r1
mov r0 runInBackground
mov r1 1
store8 r0 r1

label shellRunFdInputBackgroundCheckEnd

; Parse input - check for first/next arg
mov r0 inputBuf ; search for space
label shellCheckNextArg

mov r1 ' ' ; search for space
call strchr

cmp r1 r0 r0 ; no space?
skipneqz r1
jmp shellRunFdExecInput

mov r1 0 ; add null terminator (which acts as separator in argv string)
store8 r0 r1
inc r0

mov r1 argc ; inc argc
load8 r2 r1
inc r2
store8 r1 r2

jmp shellCheckNextArg

; Exec input (inputBuf contains argv combined string)
label shellRunFdExecInput

; Check for cd builtin
mov r0 inputBuf
mov r1 cdStr
call strcmp
cmp r0 r0 r0
skipeqz r0
jmp shellRunFdBuiltinNoCd
call shellCd
jmp shellRunFdInputLoopStart
label shellRunFdBuiltinNoCd
; Check for exit builtin
mov r0 inputBuf
mov r1 exitStr
call strcmp
cmp r0 r0 r0
skipeqz r0
jmp shellRunFdBuiltinNoExit
mov r0 0 ; exit
ret
label shellRunFdBuiltinNoExit

; Otherwise try to run as program
call shellForkExec

; Loop back to read next line
jmp shellRunFdInputLoopStart

label shellForkExec
; Fork
mov r0 SyscallIdFork
syscall
mov r1 childPid
store8 r1 r0

mov r1 PidMax
cmp r1 r0 r1
skipneq r1
jmp shellForkExecError
skipneqz r1
jmp shellForkExecForkChild
jmp shellForkExecForkParent

label shellForkExecForkChild
; Call exec
mov r0 argc
load8 r0 r0
mov r1 inputBuf
call runpath

; exec only returns in error
mov r0 execErrorStr
call puts0
mov r0 inputBuf
call puts0
mov r0 '\n'
call putc0
mov r0 SyscallIdExit
mov r1 1
syscall

label shellForkExecForkParent
; Wait for child to terminate (unless background set to true)
mov r1 runInBackground
load8 r1 r1
cmp r1 r1 r1
skipeqz r1
jmp shellRunFdRet

call waitpid ; childs PID in r0

mov r0 PidMax
mov r1 childPid
store8 r1 r0

; release interrupt lock (if set)
mov r0 interruptHandlerLock
call lockpost

label shellRunFdRet
ret

label shellForkExecError
; Print error
mov r0 forkErrorStr
call puts0

ret

label shellCd
; If path is missing or empty, use '/home'
mov r0 argc ; check if we have more than 0 args
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp shellCdHome

mov r0 inputBuf ; grab path
inc3 r0 ; skip 'cd '
call strlen
cmp r0 r0 r0
skipneqz r0
jmp shellCdHome

; Otherwise use given argument
jmp shellCdMakeAbs

label shellCdHome
mov r0 absBuf
mov r1 homeDir
call strcpy
jmp shellCdIsDir

; Make sure path is absolute
label shellCdMakeAbs
mov r0 absBuf
mov r1 inputBuf
inc3 r1
call getabspath

mov r0 absBuf
call pathnormalise

; Ensure path is a directory
label shellCdIsDir
mov r0 SyscallIdIsDir
mov r1 absBuf
syscall

cmp r0 r0 r0
skipeqz r0
jmp shellCdUpdateEnvVars

mov r0 dirNotFoundErrorStr
call puts0
mov r0 absBuf
call puts0
mov r0 '\n'
call putc0
ret

; Update environment variables
label shellCdUpdateEnvVars
mov r0 pwdBuf
mov r1 absBuf
call strcpy

mov r0 SyscallIdEnvSetPwd
mov r1 pwdBuf
syscall

ret

label suicideHandler
; TODO: Fix hack - we assume lock functions only modify r0 and r1 (and call modifies r5)
push16 r0
push16 r1
push16 r5
; already doing this? Note: we never release this lock as we exit during anyways
mov r0 suicideHandlerLock
call lockwaittry
cmp r0 r0 r0
skipneqz r0
jmp suicideHandlerRet
; Simulate an interrupt to kill child (if any)
call interruptHandler
; Jump to finish label which calls exit, so we do not need to return from this handler
jmp finish
label suicideHandlerRet
pop16 r5
pop16 r1
pop16 r0
ret

label interruptHandler
; TODO: Fix hack - we assume lock functions only modify r0 and r1 (and call modifies r2, r5)
push16 r0
push16 r1
push16 r2
push16 r5
; are we already doing this?
mov r0 interruptHandlerLock
call lockwaittry
cmp r0 r0 r0
skipneqz r0
jmp interruptHandlerRet
mov r1 childPid
load8 r1 r1
; do we not even have a child?
mov r0 PidMax
cmp r0 r1 r0
skipneq r0
jmp interruptHandlerReleaseLock
; send child suicide signal
mov r0 SyscallIdSignal
mov r2 SignalIdSuicide
syscall
; call waitpid with a 5s timeout
label interruptHandlerWaitPidLoopStart
mov r0 SyscallIdWaitPid
mov r2 5
syscall
; interrupted by another signal? if so, try again
mov r2 SyscallWaitpidStatusInterrupted
cmp r2 r0 r2
skipneq r2
jmp interruptHandlerWaitPidLoopStart
; if not timed out, no need to kill
mov r2 SyscallWaitpidStatusTimeout
cmp r2 r0 r2
skipeq r2
jmp interruptHandlerRet
; kill child
mov r0 SyscallIdKill
syscall
; note: in this case lock is released when we return from waitpid
jmp interruptHandlerRet
; release lock
label interruptHandlerReleaseLock
mov r0 interruptHandlerLock
call lockpost
; done
label interruptHandlerRet
pop16 r5
pop16 r2
pop16 r1
pop16 r0
ret
