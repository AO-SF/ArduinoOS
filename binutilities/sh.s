db stdioPath '/dev/ttyS0', 0
db initialPwd '/home', 0
db prompt '$ ', 0
db forkErrorStr 'could not fork\n', 0
db execErrorStr 'could not exec: ', 0

aw pwd 64 ; (KernelFsPathMax+1)

ab inputBuf 64 ;

jmp start

require libio.s
require libstr.s

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

; Copy initial working directory into pwd variable
mov r0 pwd
mov r1 initialPwd
call strcpy

label inputLoopStart
; Print pwd
mov r0 pwd
call puts

; Print prompt
mov r0 prompt
call puts

; Wait for input
mov r0 512
syscall
mov r1 inputBuf
mov r2 64
call fgets

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
; TODO: handle args

; Exec input (inputBuf contains executable path)
label execInput
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
mov r1 0
syscall

; Exit (failure)
label error
mov r0 0
mov r1 1
syscall

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
mov r0 5
mov r1 inputBuf
syscall

; exec only returns in error
mov r0 execErrorStr
call puts
mov r0 inputBuf
call puts
mov r0 '\n'
call putc
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
call puts

ret
