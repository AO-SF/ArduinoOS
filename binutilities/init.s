db stdioPath '/dev/ttyS0'

db msg 'Hello world from init!\n'
dw msgLen 23

; Open stdin/stdout
mov r0 258
mov r1 stdioPath
syscall

; Check for bad file-descriptor
cmp r1 r0 r0
skipneq r1
jmp error

; Write message
mov r0 257
; r1 already contains fd
mov r2 msg
mov r3 msgLen
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
