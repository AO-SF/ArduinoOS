db stdioPath '/dev/ttyS0', 0
db initialPwd '/home', 0
db prompt '$ ', 0

aw stdioFd 1

aw pwd 64 ; (KernelFsPathMax+1)

ab inputBuf 64 ;

jmp start

include libio.s
include libstr.s

label start

; Open stdin/stdout
mov r0 258
mov r1 stdioPath
syscall

mov r1 stdioFd
store16 r1 r0

; Check for bad file-descriptor
cmp r1 r0 r0
skipneqz r1
jmp error

; Copy initial working directory into pwd variable
mov r0 pwd
mov r1 initialPwd
call strcpy

label inputLoopStart
; Print pwd
mov r0 stdioFd
load16 r0 r0
mov r1 pwd
call fputs

; Print prompt
mov r0 stdioFd
load16 r0 r0
mov r1 prompt
call fputs

; Wait for input
mov r0 stdioFd
load16 r0 r0
mov r1 inputBuf
mov r2 64
call fgets

; Parse input
; TODO: this (for now just echo back)
mov r0 stdioFd
load16 r0 r0
mov r1 inputBuf
call fputs

; Loop back to read next line
jmp inputLoopStart

; End of input loop
label inputLoopEnd

; Close stdin/stdout
mov r0 259
mov r1 stdioFd
load16 r1 r1
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
