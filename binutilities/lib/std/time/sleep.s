; sleep(seconds=r0) - sleep for (at least) given number of seconds
label sleep

; call waitpid on init process (which never dies), but with a timeout of the given number of seconds
mov r2 r0
mov r0 6
mov r1 0
syscall

ret
