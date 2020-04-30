require ../str/strcat.s
require ../str/strcpy.s
require getpwd.s

db libprocSlashStr '/', 0

; getabspath(r0=dest addr, r1=src path addr) - checks if path is absolute, and if not makes it so, stores result in dest addr either way
label getabspath

; Set r0 to empty string
mov r2 0
store8 r0 r2

; Test first char for '/'
load8 r2 r1
mov r3 '/'
cmp r2 r2 r3
skipneq r2
jmp getabspathnext ; already has a '/', continue

; Add pwd
push16 r1
push16 r0
call getpwd
mov r1 r0
pop16 r0
push16 r0
call strcpy
pop16 r0
pop16 r1

; Add '/'
push16 r0
push16 r1
mov r1 libprocSlashStr
call strcat
pop16 r1
pop16 r0

label getabspathnext
; Add src string to dest (r0 and r1 already setup)
call strcat

ret
