db stdioPath '/dev/ttyS0', 0
db prompt '$ ', 0
db forkErrorStr 'could not fork\n', 0
db execErrorStr 'could not exec: ', 0
db cdStr 'cd', 0
db nullChar 0
db homeDir '/home', 0
db slashStr '/', 0

ab inputBuf 64
ab absBuf 64
ab envPathBuf 64
aw arg1Ptr 1

jmp start

require lib/std/io/fput.s
require lib/std/io/fget.s
require lib/std/proc/getabspath.s
require lib/std/proc/getpwd.s
require lib/std/proc/exit.s
require lib/std/str/strtrimlast.s
require lib/std/str/strchr.s
require lib/std/str/strequal.s

label start

; Open stdin/stdout
mov r0 258
mov r1 stdioPath
syscall

; Check for bad file-descriptor
cmp r1 r0 r0
skipneqz r1
jmp error

; Save stdio fd to environment variables
mov r1 r0
mov r0 513
syscall

label inputLoopStart
; Print pwd (reuse inputBuf to save space)
mov r0 inputBuf
call getpwd
mov r0 inputBuf
call puts0

; Print prompt
mov r0 prompt
call puts0

; Wait for input
mov r0 512
syscall
mov r1 0
mov r2 inputBuf
mov r3 64
call fgets

; Parse input - clear arg1Ptr
mov r0 arg1Ptr
mov r1 nullChar
store16 r0 r1

; Parse input - trim trailing newline
mov r0 inputBuf
call strtrimlast

; Parse input - check for first space implying there may be arguments
mov r0 inputBuf
mov r1 ' '
call strchr

cmp r1 r0 r0
skipneqz r1
jmp execInput ; if no space found then no arguments

; Parse input - space found
; terminate executable name
mov r1 0
store8 r0 r1

; arg found - store ptr to it
inc r0
mov r1 arg1Ptr
store16 r1 r0

; Exec input (inputBuf contains executable path)
label execInput

; Check for builtin
mov r0 inputBuf
mov r1 cdStr
call strequal

cmp r0 r0 r0
skipeqz r0
jmp shellCd ; this jumps back to inputLoopStart

; Otherwise try to run as program
call shellForkExec

; Loop back to read next line
jmp inputLoopStart

; End of input loop
label inputLoopEnd

; Close stdin/stdout
mov r0 512
syscall
mov r1 r0
mov r0 259
syscall

; Exit (success)
mov r0 0
call exit

; Exit (failure)
label error
mov r0 1
call exit

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
; Check if the inputBuf contains no slashes,
; If this is true, we try first looking in PATH for the executable.
mov r0 inputBuf
mov r1 '/'
call strchr

cmp r0 r0 r0
skipeqz r0
jmp shellForkExec2ndTry

; No slashes found, create combined path to try
mov r0 516 ; getpath
mov r1 envPathBuf
syscall
mov r0 envPathBuf
mov r1 slashStr
call strcat
mov r0 envPathBuf
mov r1 inputBuf
call strcat

; Call exec
mov r0 5
mov r1 envPathBuf
mov r2 arg1Ptr ; // TODO: Only send if not empty
load16 r2 r2
syscall

; Make sure path is absolute
label shellForkExec2ndTry
mov r0 absBuf
mov r1 inputBuf
call getabspath

; Call exec
mov r0 5
mov r1 absBuf
mov r2 arg1Ptr ; // TODO: Only send if not empty
load16 r2 r2
syscall

; exec only returns in error
mov r0 execErrorStr
call puts0
mov r0 absBuf
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

jmp inputLoopStart

label shellCdHome

mov r0 515
mov r1 homeDir
syscall

jmp inputLoopStart
