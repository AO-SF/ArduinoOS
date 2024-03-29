require ../../sys/sys.s

requireend ../proc/getabspath.s
requireend ../mem/memmove.s
requireend ../str/strcat.s
requireend ../str/strchr.s
requireend ../str/strcpy.s

ab getpathScratchBuf PathMax
db getpathSlashStr '/', 0

; getpath(r0=dest path, r1=src path) - interprets path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd
label getpath
; Copy src path into dest and getpathScratchBuf
push16 r0
push16 r1
call strcpy
pop16 r1
push16 r1
mov r0 getpathScratchBuf
call strcpy
pop16 r1
pop16 r0
; Check if the path contains no slashes,
push16 r0
push16 r1
mov r1 '/'
call strchr
; If this is true, we try first looking in PATH for the file.
cmp r2 r0 r0
pop16 r1
pop16 r0
skipeqz r2
jmp getpathMakeAbsoluteNoStackRestore
; No slashes found,
; look for directories in PATH environment variable
mov r2 r0
mov r3 r1
mov r0 SyscallIdEnvGetPath
syscall
mov r1 r0
mov r0 r2
mov r2 r1 ; PATH now in r2
mov r1 r3
label getpathPATHLoopStart
; look for colon indicating end of path dir
push16 r0
push16 r1
push16 r2
mov r0 r2
mov r1 ':'
call strchr
mov r3 r0 ; r3 now points to first/next colon in PATH
cmp r4 r3 r3
pop16 r2
pop16 r1
pop16 r0
skipneqz r4
jmp getpathMakeAbsoluteStackRestore ; no colon found so we have reached the end of the PATH directory list
; colon found - create combined string from PATH dir and src string
push16 r3
push16 r2
push16 r0
push16 r1
; add current directory from PATH (r2 contains start of directory entry, while r3 points to the terminating colon which is immediately after)
mov r0 getpathScratchBuf
sub r4 r3 r2 ; compute length of PATH sub part
add r0 r0 r4
mov r4 0
store8 r0 r4 ; store null byte ready to terminate new string
mov r0 getpathScratchBuf
mov r1 r2
sub r2 r3 r2 ; recalculate length of PATH part
call memmove ; copy current part of PATH to start of scratch buf, ignoring terminating colon
; add joining slash
mov r0 getpathScratchBuf
mov r1 getpathSlashStr
call strcat
; add user provided src path
mov r0 getpathScratchBuf
pop16 r1
push16 r1
call strcat
; restore registers and stack
pop16 r1
pop16 r0
pop16 r2
pop16 r3
; test if combined path exists
push16 r0
push16 r1
mov r0 SyscallIdFileExists
mov r1 getpathScratchBuf
syscall
cmp r4 r0 r0
pop16 r1
pop16 r0
skipneqz r4
jmp getpathPATHLoopContinue
; it does - copy path into dest in r0
mov r1 getpathScratchBuf
call strcpy
ret
; it does not - update offset in r2 for next time and loop again
label getpathPATHLoopContinue
mov r2 r3
inc r2
jmp getpathPATHLoopStart
; Otherwise make sure dest path is absolute
label getpathMakeAbsoluteStackRestore
call getabspath
ret
label getpathMakeAbsoluteNoStackRestore
call getabspath
ret
