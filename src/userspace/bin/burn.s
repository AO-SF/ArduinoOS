; Register suicide signal handler
mov r0 1024
mov r1 3 ; suicide signal id
mov r2 suicideHandler
syscall

; Simply loop forever
label loop
jmp loop

; Suicide handler to exit quickly
label suicideHandler
mov r0 0
mov r1 0
syscall
