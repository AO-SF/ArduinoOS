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
db lessPath 'less',0
db emptyStr 0

ab pageArgBuf ArgLenMax

ab pathBuf ArgLenMax
ab argvBuffer ArgLenMax ; TODO: should be ArgLenMax+5 at least? (for 'less\0'), can also avoid using two buffers

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Get args (done in reverse so we can get off stack in correct order)
mov r0 SyscallIdArgvN ; get page string
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage
push16 r0

mov r0 SyscallIdArgvN ; grab section number as string
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage
push16 r0

; Create path where manual will exist if it does
mov r0 pathBuf ; start with base path
mov r1 basePath
call strcpy
mov r0 pathBuf ; then add section number
pop16 r1
call strcat
mov r0 pathBuf ; then add a slash
mov r1 slashStr
call strcat
mov r0 pathBuf ; finally add page
pop16 r1
call strcat

; Check file exists
mov r0 SyscallIdFileExists
mov r1 pathBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp notFound

; create argv for exec call to less
mov r0 argvBuffer ; start with 'less' with a terminating null byte
mov r1 lessPath
call strcpy

mov r0 argvBuffer ; append path as argument
inc5 r0
mov r1 pathBuf
call strcpy

; exec to 'less' to display file
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
