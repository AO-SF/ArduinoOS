require ../../sys/proc.s

label forkexec ; takes path in r0 to exec in a forked process, with arguments in r1-r3, and returns childs PID in r0 (in the parent), or 0 on failure
; save path and arguments
push r0
push r1
; call fork
mov r0 4
syscall
mov r1 PidMax
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
mov r4 r3
mov r3 r2
pop r2
pop r1
syscall
; exec only returns on failure
mov r0 1
call exit
; success in the parent - restore stack and return success (r0 already contains PID of new child)
label forkexecsuccess
pop r2
pop r1
ret

; error forking in the parent - restore stack and return failure
label forkexecerror
pop r2
pop r1
mov r0 0
ret
