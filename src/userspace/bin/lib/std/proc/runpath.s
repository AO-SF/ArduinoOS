require ../proc/getpath.s

ab runpathPathBuf 64

; runpath(r0=path, r1=arg1, r2=arg2, r3=arg3) - executes path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. on sucess does not return
label runpath
; Copy path and args so we can modify
push r3
push r2
push r1
mov r1 r0
mov r0 runpathPathBuf
call getpath

; Call exec
mov r0 5
mov r1 runpathPathBuf
pop r2
pop r3
pop r4
syscall
ret
