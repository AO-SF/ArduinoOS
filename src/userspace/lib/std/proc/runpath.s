require ../proc/getabspath.s
require ../str/strcat.s
require ../str/strchr.s
require ../str/strcpy.s

ab runpathPathBuf 64
ab runpathArg1 64
ab runpathArg2 64
ab runpathArg3 64
ab runpathScratchBuf 64
db runpathSlashStr '/', 0

; runpath(r0=path, r1=arg1, r2=arg2, r3=arg3) - executes path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. on sucess does not return
label runpath

; Copy paths so we can modify
push r3
push r2
push r1

mov r1 r0
mov r0 runpathPathBuf
call strcpy

pop r1
mov r0 runpathArg1
call strcpy

pop r1
mov r0 runpathArg2
call strcpy

pop r1
mov r0 runpathArg3
call strcpy

; Check if the path contains no slashes,
; If this is true, we try first looking in PATH for the executable.
mov r0 runpathPathBuf
mov r1 '/'
call strchr

cmp r0 r0 r0
skipeqz r0
jmp runpath2ndTry

; No slashes found, create combined path to try
mov r0 516 ; getpath
mov r1 runpathScratchBuf
syscall
mov r0 runpathScratchBuf
mov r1 runpathSlashStr
call strcat
mov r0 runpathScratchBuf
mov r1 runpathPathBuf
call strcat

; Call exec
mov r0 5
mov r1 runpathScratchBuf
mov r2 runpathArg1
mov r3 runpathArg2
mov r4 runpathArg3
syscall

; Make sure path is absolute
label runpath2ndTry
mov r0 runpathScratchBuf
mov r1 runpathPathBuf
call getabspath

; Call exec
mov r0 5
mov r1 runpathScratchBuf
mov r2 runpathArg1
mov r3 runpathArg2
mov r4 runpathArg3
syscall

ret
