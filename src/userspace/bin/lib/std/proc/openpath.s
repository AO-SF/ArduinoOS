require ../../sys/sys.s

requireend ../proc/getpath.s

; openpath(r0=path) - opens path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. returns fd on success (0/invalid on failure)
label openpath
; Convert path as shell would
mov r1 r0
mov r0 r6 ; use stack to store string copy
mov r2 PathMax
add r6 r6 r2
call getpath
; Try to open
mov r0 SyscallIdOpen
mov r2 PathMax
sub r6 r6 r2
mov r1 r6
syscall
ret
