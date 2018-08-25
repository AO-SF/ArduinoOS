require ../proc/getpath.s

ab runpathPathBuf PathMax

; runpath(r0=path, r1=arg1, r2=arg2, r3=arg3) - executes path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. on sucess does not return
label runpath
; Copy path and args so we can modify
push16 r3
push16 r2
push16 r1
mov r1 r0
mov r0 runpathPathBuf
call getpath

; Call exec
mov r0 5
mov r1 runpathPathBuf
pop16 r2
pop16 r3
pop16 r4
syscall
ret
