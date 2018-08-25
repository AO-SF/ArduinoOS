require ../../sys/sys.s

requireend ../proc/getabspath.s
requireend ../str/strcat.s
requireend ../str/strchr.s
requireend ../str/strcpy.s

ab getpathPATHBuf EnvVarPathMax
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
jmp getpathMakeAbsolute
; No slashes found,
; look for directories in PATH environment variable
push16 r0
push16 r1
mov r0 516 ; getpath
mov r1 getpathPATHBuf
syscall
pop16 r1
pop16 r0
mov r2 getpathPATHBuf ; // ptr into PATH
label getpathPATHLoopStart
; look for colon indicating end of path dir
push16 r0
push16 r1
push16 r2
mov r0 r2
mov r1 ':'
call strchr
mov r3 r0
cmp r4 r3 r3
pop16 r2
pop16 r1
pop16 r0
skipneqz r4
jmp getpathMakeAbsolute
; colon found - replace with null byte
push16 r0
push16 r1
push16 r2
mov r4 0
store8 r3 r4
pop16 r2
pop16 r1
pop16 r0

; create combined string from PATH dir and src string
push16 r3
push16 r2
push16 r0
push16 r1
mov r0 getpathScratchBuf
mov r1 r2
call strcpy
mov r0 getpathScratchBuf
mov r1 getpathSlashStr
call strcat
mov r0 getpathScratchBuf
pop16 r1
push16 r1
call strcat
pop16 r1
pop16 r0
pop16 r2
pop16 r3
; test if combined path exists
push16 r0
push16 r1
push16 r2
push16 r3
mov r0 266
mov r1 getpathScratchBuf
syscall
cmp r4 r0 r0
pop16 r3
pop16 r2
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
label getpathMakeAbsolute
call getabspath
ret
