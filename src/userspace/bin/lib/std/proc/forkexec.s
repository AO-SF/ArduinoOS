label forkexec ; takes path in r0 to exec in a forked process, with arguments in r1-r3, and returns boolean result in r0 (in the parent)
; save path and arguments
push r0
push r1
push r2
push r3
; call fork
mov r0 4
syscall
mov r1 64 ; PidMax
cmp r1 r0 r1
skipneq r1
jmp forkexecerror
cmp r1 r0 r0
skipneqz r1
jmp forkexecchild
jmp forkexecsuccess
; child - exec given program with given args
label forkexecchild
mov r0 5
pop r4
pop r3
pop r2
pop r1
syscall
; exec only returns on failure
mov r0 1
call exit
; success in the parent - restore stack and return success
label forkexecsuccess
pop r4
pop r3
pop r2
pop r1
mov r0 1
ret

; error forking in the parent - restore stack and return failure
label forkexecerror
pop r4
pop r3
pop r2
pop r1
mov r0 0
ret
