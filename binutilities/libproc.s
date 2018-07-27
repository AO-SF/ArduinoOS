; getpwd(r0=dest addr)
label getpwd
mov r1 r0
mov r0 514
syscall
ret

; exit (r0=status) - does not return
label exit
mov r1 r0
mov r0 0
syscall
