require lib/sys/sys.s

db stdinPath '/dev/ttyS0', 0
db stdoutPath '/dev/ttyS0', 0
db prompt '$ ', 0
db forkErrorStr 'could not fork\n', 0
db execErrorStr 'could not exec: ', 0
db dirNotFoundErrorStr 'no such directory: ', 0
db cdStr 'cd', 0
db exitStr 'exit', 0
db emptyStr 0
db homeDir '/home', 0

ab handlingStdio 1
const inputBufLen 128
ab inputBuf inputBufLen
ab absBuf PathMax
ab pwdBuf PathMax
aw arg1Ptr 1
aw arg2Ptr 1
aw arg3Ptr 1

ab interactiveMode 1
ab inputFd 1
aw readOffset 1

ab runInBackground 1

ab childPid 1

ab suicideHandlerLock 1
ab interruptHandlerLock 1

requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/getpwd.s
requireend lib/std/proc/openpath.s
requireend lib/std/proc/runpath.s
requireend lib/std/str/strchr.s
requireend lib/std/str/strrchr.s
requireend lib/std/str/strequal.s
requireend lib/std/str/strtrimlast.s

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
mov r1 PathMax
store8 r0 r1

; Register suicide signal handler
mov r0 1024
mov r1 3 ; suicide signal id
mov r2 suicideHandlerTrampoline
syscall

; Register interrupt signal handler
mov r0 1024
mov r1 0 ; interrupt signal id
mov r2 interruptHandlerTrampoline
syscall

; Is stdinfd already sensible?
; TODO: We should probably check both stdin and stdout
mov r0 handlingStdio
mov r1 0
store8 r0 r1

mov r0 SyscallIdEnvGetStdinFd
syscall
cmp r0 r0 r0
skipeqz r0
jmp startDone

; No - open stdin and stdout, storing fds into environment variables
mov r0 handlingStdio
mov r1 1
store8 r0 r1

mov r0 SyscallIdOpen
mov r1 stdinPath
syscall
cmp r1 r0 r0
skipneqz r1
jmp exiterror
mov r1 r0
mov r0 SyscallIdEnvSetStdinFd
syscall

mov r0 SyscallIdOpen
mov r1 stdoutPath
syscall
cmp r1 r0 r0
skipneqz r1
jmp exiterror
mov r1 r0
mov r0 SyscallIdEnvSetStdoutFd
syscall

label startDone

; Check for scripts passed as arguments
mov r1 1 ; child loop index
label argLoopStart
push8 r1
mov r0 3
mov r2 inputBuf
syscall

; No argument?
cmp r0 r0 r0
skipneqz r0
jmp argLoopEnd

; Open file
mov r0 inputBuf
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
mov r0 SyscallIdEnvGetStdinFd
syscall
mov r1 inputFd
store8 r1 r0

mov r0 1
mov r1 interactiveMode
store8 r1 r0

call shellRunFd

label finish
; Close stdin/stdout (if we are handling)
mov r0 handlingStdio
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp exitsuccess

mov r0 SyscallIdEnvGetStdinFd
syscall
mov r1 r0
mov r0 SyscallIdClose
syscall

mov r0 SyscallIdEnvGetStdoutFd
syscall
mov r1 r0
mov r0 SyscallIdClose
syscall

; Exit (success)
label exitsuccess
mov r0 0
call exit

; Exit (failure)
label exiterror
mov r0 1
call exit

label shellRunFd
mov r0 readOffset
mov r1 0
store16 r0 r1
label shellRunFdInputLoopStart

; Only print prompt in interactive mode
mov r0 interactiveMode
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp shellRunFdInputPromptEnd

; Print pwd (reuse inputBuf to save space)
mov r0 inputBuf
call getpwd
mov r0 inputBuf
call puts0

; Print prompt
mov r0 prompt
call puts0
label shellRunFdInputPromptEnd

; Read input
mov r0 inputFd
load8 r0 r0
mov r1 readOffset
load16 r1 r1
mov r2 inputBuf
mov r3 inputBufLen
call fgets

; Update read offset for next time
mov r2 readOffset
load16 r1 r2
add r1 r1 r0
store16 r2 r1

; If in file mode and empty read then EOF
cmp r0 r0 r0
skipeqz r0
jmp shellRunFdInputNoEof
mov r0 interactiveMode
load8 r0 r0
cmp r0 r0 r0
skipeqz r0
jmp shellRunFdInputNoEof
mov r0 1 ; continue
ret
label shellRunFdInputNoEof

; Parse input - clear arg pointers
mov r1 emptyStr
mov r0 arg1Ptr
store16 r0 r1
mov r0 arg2Ptr
store16 r0 r1
mov r0 arg3Ptr
store16 r0 r1

; Parse input - trim trailing newline
mov r0 inputBuf
call strtrimlast

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

; Parse input - check for first space implying at least one argument
mov r0 inputBuf
mov r1 ' '
call strchr

cmp r1 r0 r0
skipneqz r1
jmp shellRunFdExecInput

mov r1 0
store8 r0 r1

inc r0
mov r1 arg1Ptr
store16 r1 r0

; check another another arg (2nd after cmd)
; r0 already contains ptr to previous arg
mov r1 ' '
call strchr

cmp r1 r0 r0
skipneqz r1
jmp shellRunFdExecInput

mov r1 0
store8 r0 r1

inc r0
mov r1 arg2Ptr
store16 r1 r0

; check another another arg (3rd after cmd)
; r0 already contains ptr to previous arg
mov r1 ' '
call strchr

cmp r1 r0 r0
skipneqz r1
jmp shellRunFdExecInput

mov r1 0
store8 r0 r1

inc r0
mov r1 arg3Ptr
store16 r1 r0

; Exec input (inputBuf contains executable path)
label shellRunFdExecInput

; Check for builtin
mov r0 inputBuf
mov r1 cdStr
call strequal
cmp r0 r0 r0
skipneqz r0
jmp shellRunFdBuiltinNoCd
call shellCd
jmp shellRunFdInputLoopStart
label shellRunFdBuiltinNoCd

mov r0 inputBuf
mov r1 exitStr
call strequal
cmp r0 r0 r0
skipneqz r0
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
mov r0 4
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
mov r0 inputBuf
mov r1 arg1Ptr
load16 r1 r1
mov r2 arg2Ptr
load16 r2 r2
mov r3 arg3Ptr
load16 r3 r3
call runpath

; exec only returns in error
mov r0 execErrorStr
call puts0
mov r0 inputBuf
call puts0
mov r0 '\n'
call putc0
mov r0 0
mov r1 1
syscall

label shellForkExecForkParent
; Wait for child to terminate (unless background set to true)
mov r1 runInBackground
load8 r1 r1
cmp r1 r1 r1
skipeqz r1
jmp shellRunFdRet

mov r1 r0 ; childs PID
mov r0 6 ; waitpid syscall
mov r2 0 ; infinite timeout
syscall

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

; If path is empty, use '/home'
mov r0 arg1Ptr
load16 r0 r0
call strlen

cmp r0 r0 r0
skipeqz r0
jmp shellCdNotHome

mov r0 absBuf
mov r1 homeDir
call strcpy
jmp shellCdHome

; Make sure path is absolute
label shellCdNotHome
mov r0 absBuf
mov r1 arg1Ptr
load16 r1 r1
call getabspath

label shellCdHome
; Ensure path is a directory
mov r0 265
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

mov r0 515
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
; TODO: Fix hack - we assume lock functions only modify r0 and r1 (and call modifies r5)
push16 r0
push16 r1
push16 r5
; are we already doing this?
mov r0 interruptHandlerLock
call lockwaittry
cmp r0 r0 r0
skipneqz r0
jmp interruptHandlerRet
; do we not even have a child?
mov r1 childPid
load8 r1 r1
mov r0 PidMax
cmp r0 r1 r0
skipneq r0
jmp interruptHandlerReleaseLock
; kill child
mov r0 10
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
pop16 r1
pop16 r0
ret
