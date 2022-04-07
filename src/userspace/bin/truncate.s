require lib/sys/sys.s

requireend lib/std/int32/int32str.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: truncate size file\n',0

ab pathAbsBuf ArgLenMax

aw newSize 2 ; 32 bit integer

; Grab size arg and convert to integer
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp usage

mov r1 r0
mov r0 newSize
call int32fromStr

; Grab path argument and make absolute
mov r0 SyscallIdArgvN
mov r1 2
syscall

cmp r1 r0 r0
skipneqz r1
jmp usage

mov r1 r0
mov r0 pathAbsBuf
call getabspath

; Resize file
mov r0 SyscallIdResizeFile32
mov r1 pathAbsBuf
mov r2 newSize
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
