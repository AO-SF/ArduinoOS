requireend ../proc/getpath.s

ab openpathScratchBuf 64

; openpath(r0=path) - opens path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. returns fd on success (0/invalid on failure)
label openpath
; Convert path as shell would
mov r1 r0
mov r0 openpathScratchBuf
call getpath
; Try to open
mov r0 258
mov r1 openpathScratchBuf
syscall
ret
