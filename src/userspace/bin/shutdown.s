; Invoke shutdown syscall
mov r0 1280
syscall

; Call exit in case shutdown is not immediate
mov r0 0
mov r1 0
syscall
