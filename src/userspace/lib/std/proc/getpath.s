require ../proc/getabspath.s
require ../str/strcat.s
require ../str/strchr.s
require ../str/strcpy.s

ab getpathScratchBuf 64
db getpathSlashStr '/', 0

; getpath(r0=dest path, r1=src path) - interprets path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd
label getpath
; Copy src path into dest
push r0
call strcpy
pop r0

; Check if the path contains no slashes,
push r0
mov r1 '/'
call strchr
; If this is true, we try first looking in PATH for the file.
cmp r2 r0 r0
pop r0
skipeqz r2
jmp getpathTry

; No slashes found, create combined path to try
push r0
mov r0 516 ; getpath
mov r1 getpathScratchBuf
syscall
mov r0 getpathScratchBuf
mov r1 getpathSlashStr
call strcat
mov r0 getpathScratchBuf
pop r1
push r1
call strcat
pop r0

; Check if exists
label getpathTry
push r0
mov r0 266
mov r1 getpathScratchBuf
syscall
cmp r2 r0 r0
pop r0
skipneqz r2
jmp getpathMakeAbsolute

; It does - copy path into dest in r0
mov r1 getpathScratchBuf
call strcpy
ret

; Otherwise make sure dest path is absolute
label getpathMakeAbsolute
mov r1 getpathScratchBuf
call getabspath
ret
