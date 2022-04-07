require lib/sys/sys.s

requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/waitpid.s

db pipeErrorStr 'could not open pipe\n', 0
db forkErrorStr 'could not fork\n', 0
db childStr 'Hello world!\n', 0

ab childPid 1
ab pipeReadFd 1
ab pipeWriteFd 1

const parentBufLen 32
ab parentBuf parentBufLen

; Clear child PID
mov r0 childPid
mov r1 PidMax
store8 r0 r1

; Create pipe
mov r0 SyscallIdPipeOpen
mov r1 pipeReadFd
mov r2 pipeWriteFd
syscall

cmp r0 r0 r0
skipneqz r0
jmp pipeError

; Fork
mov r0 SyscallIdFork
syscall
mov r1 childPid
store8 r1 r0

mov r1 PidMax
cmp r1 r0 r1
skipneq r1
jmp forkError
skipneqz r1
jmp forkChild
jmp forkParent

; Child
label forkChild
; Close read end of pipe
mov r0 SyscallIdClose
mov r1 pipeReadFd
load8 r1 r1
syscall
; Write string into pipe
mov r0 pipeWriteFd
load8 r0 r0
mov r1 0
mov r2 childStr
call fputs
; Child exit
mov r0 0
call exit

; Parent
label forkParent
; Close write end of pipe
mov r0 SyscallIdClose
mov r1 pipeWriteFd
load8 r1 r1
syscall
; Wait for child to terminate
mov r0 childPid
load8 r0 r0
call waitpid
; Read from pipe and print to stdout
mov r0 pipeReadFd
load8 r0 r0
mov r1 0
mov r2 parentBuf
mov r3 parentBufLen
call fgets
mov r0 parentBuf
call puts0
; Parent exit
mov r0 0
call exit

; Errors

label pipeError
; Print error
mov r0 pipeErrorStr
call puts0
; Exit
mov r0 0
call exit

label forkError
; Print error
mov r0 forkErrorStr
call puts0
; Exit
mov r0 0
call exit
