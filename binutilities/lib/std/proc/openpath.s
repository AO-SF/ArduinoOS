require ../proc/getabspath.s
require ../str/strcat.s
require ../str/strchr.s
require ../str/strcpy.s

ab openpathPathBuf 64
ab openpathScratchBuf 64
db openpathSlashStr '/', 0

; openpath(r0=path) - opens path as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. on sucess does not return
label openpath

; Copy path so we can modify
mov r1 r0
mov r0 openpathPathBuf
call strcpy

; Check if the path contains no slashes,
; If this is true, we try first looking in PATH for the executable.
mov r0 openpathPathBuf
mov r1 '/'
call strchr

cmp r0 r0 r0
skipeqz r0
jmp openpathTry

; No slashes found, create combined path to try
mov r0 516 ; getpath
mov r1 openpathScratchBuf
syscall
mov r0 openpathScratchBuf
mov r1 openpathSlashStr
call strcat
mov r0 openpathScratchBuf
mov r1 openpathPathBuf
call strcat

; Try to open
mov r0 258
mov r1 openpathScratchBuf
syscall

cmp r1 r0 r0
skipeqz r1
jmp openPathRet

; Make sure path is absolute
label openpathTry
mov r0 openpathScratchBuf
mov r1 openpathPathBuf
call getabspath

; Try to open
mov r0 258
mov r1 openpathScratchBuf
syscall

label openPathRet
ret
