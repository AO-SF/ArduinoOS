require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: truncate size file\n',0

ab sizeArgBuf ArgLenMax
ab pathArgBuf ArgLenMax
ab pathAbsBuf ArgLenMax

; Grab args
mov r0 SyscallIdArgvN
mov r1 1
mov r2 sizeArgBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage
mov r0 SyscallIdArgvN
mov r1 2
mov r2 pathArgBuf
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

; Convert size to integer
mov r0 sizeArgBuf
call strtoint
push8 r0

; Make path absolute
mov r0 pathAbsBuf
mov r1 pathArgBuf
call getabspath

; Resize file
mov r0 SyscallIdResizeFile
mov r1 pathAbsBuf
pop8 r2
syscall

cmp r0 r0 r0
skipneqz r0
jmp failure

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
label failure
mov r0 1
call exit
