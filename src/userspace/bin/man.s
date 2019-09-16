require lib/sys/sys.s

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

ab sectionArgBuf ArgLenMax
ab pageArgBuf ArgLenMax

ab pathBuf ArgLenMax
ab argvBuffer ArgLenMax ; TODO: should be ArgLenMax+5 at least? (for 'cat '), can also avoid using two buffers

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Get args
mov r0 SyscallIdArgvN ; grab section number as string
mov r1 1
mov r2 sectionArgBuf
mov r3 ArgLenMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

mov r0 SyscallIdArgvN ; get page string
mov r1 2
mov r2 pageArgBuf
mov r3 ArgLenMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Create path where manual will exist if it does
mov r0 pathBuf ; start with base path
mov r1 basePath
call strcpy
mov r0 pathBuf ; then add section number
mov r1 sectionArgBuf
call strcat
mov r0 pathBuf ; then add a slash
mov r1 slashStr
call strcat
mov r0 pathBuf ; finally add page
mov r1 pageArgBuf
call strcat

; Check file exists
mov r0 SyscallIdFileExists
mov r1 pathBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp notFound

; create argv for exec call to cat
mov r0 argvBuffer ; start with 'cat' with a terminating null byte
mov r1 catPath
call strcpy

mov r0 argvBuffer ; append path as argument
inc4 r0
mov r1 pathBuf
call strcpy

; exec to cat to display file
mov r0 2
mov r1 argvBuffer
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
