requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/runpath.s
requireend lib/std/str/strcat.s
requireend lib/std/str/strcpy.s

db usageStr 'usage: man section page\n',0
db notFoundStr 'error: not such manual entry\n',0
db execStr 'error: could not display manual\n',0
db basePath '/usr/man/',0
db slashStr '/',0
db catPath 'cat',0
db emptyStr 0

ab sectionArgBuf 64
ab pageArgBuf 64

ab pathBuf 64

; Get args
mov r0 3
mov r1 1
mov r2 sectionArgBuf
mov r3 64
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage
mov r0 3
mov r1 2
mov r2 pageArgBuf
mov r3 64
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Create path where manual will exist if it does
mov r0 pathBuf
mov r1 basePath
call strcpy
mov r0 pathBuf
mov r1 sectionArgBuf
call strcat
mov r0 pathBuf
mov r1 slashStr
call strcat
mov r0 pathBuf
mov r1 pageArgBuf
call strcat

; Check file exists
mov r0 266
mov r1 pathBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp notFound

; exec to cat to display file
mov r0 catPath
mov r1 pathBuf
mov r2 emptyStr
mov r3 emptyStr
call runpath

; Exec only fails on error
mov r0 execStr
call puts0
mov r0 1
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit

label notFound
mov r0 notFoundStr
call puts0
mov r0 1
call exit
