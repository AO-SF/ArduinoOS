db stdioPath '/dev/ttyS0', 0
db prompt '$ ', 0
db forkErrorStr 'could not fork\n', 0
db execErrorStr 'could not exec: ', 0
db cdStr 'cd', 0
db exitStr 'exit', 0
db emptyStr 0
db homeDir '/home', 0

ab handlingStdio 1
ab inputBuf 64
ab absBuf 64
aw arg1Ptr 1
aw arg2Ptr 1
aw arg3Ptr 1

ab interactiveMode 1
ab inputFd 1
aw readOffset 1

jmp start

require lib/std/io/fget.s
require lib/std/io/fput.s
require lib/std/io/fputdec.s
require lib/std/proc/exit.s
require lib/std/proc/getabspath.s
require lib/std/proc/getpwd.s
require lib/std/proc/runpath.s
require lib/std/str/strchr.s
require lib/std/str/strequal.s
require lib/std/str/strtrimlast.s

label start

; Is stdiofd already sensible?
mov r0 handlingStdio
mov r1 0
store8 r0 r1

mov r0 512
syscall
cmp r0 r0 r0
skipeqz r0
jmp startDone

; No - open it
mov r0 258
mov r1 stdioPath
syscall

; Check for bad file-descriptor
cmp r1 r0 r0
skipneqz r1
jmp exiterror

; Save stdio fd to environment variables
mov r1 r0
mov r0 513
syscall

label startDone

; Check for scripts passed as arguments
mov r1 1 ; child loop index
label argLoopStart
push r1
mov r0 3
mov r2 inputBuf
syscall

; No argument?
cmp r0 r0 r0
skipneqz r0
jmp argLoopEnd

; Ensure path is absolute
mov r0 absBuf
mov r1 inputBuf
call getabspath

; Open file
mov r0 258
mov r1 absBuf
syscall

mov r1 inputFd
store8 r1 r0

; Run script
mov r0 0
mov r1 interactiveMode
store8 r1 r0
call shellRunFd

; Close file
mov r0 259
mov r1 inputFd
load8 r1 r1
syscall

; Advance to next arg
pop r1
inc r1
jmp argLoopStart
label argLoopEnd
pop r1

; Call shellRunFd on stdio fd
mov r0 512
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

mov r0 512
syscall
mov r1 r0
mov r0 259
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
mov r3 64
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

mov r1 64 ; ProcManPidMax
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
; Wait for child to terminate
mov r1 r0 ; childs PID
mov r0 6 ; waitpid syscall
syscall
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
skipneqz r0
jmp shellCdHome

; Make sure path is absolute
mov r0 absBuf
mov r1 arg1Ptr
load16 r1 r1
call getabspath

; Update environment variables
mov r0 515
mov r1 absBuf
syscall

ret

label shellCdHome

mov r0 515
mov r1 homeDir
syscall

ret
