; Example init file - calls fork & exec to run another program

db execPath '/bin/helloworld2.o', 0

; Call fork
mov r0 4
syscall

; Check fork return
mov r1 64 ; ProcManPidMax
cmp r1 r0 r1
skipneqz r1
jmp forkRetChild
skipneq r1
jmp forkRetFail

label forkRetParent
jmp success;

label forkRetChild
; Exec
mov r0 5
mov r1 execPath
syscall
jmp error ; exec only returns on failure

label forkRetFail
jmp error;

; Exit (success)
label success
mov r0 0
mov r1 0
syscall

; Exit (failure)
label error
mov r0 0
mov r1 1
syscall
