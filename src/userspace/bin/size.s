require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/int32/int32mem.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s

db usageStr 'usage: size path\n', 0

ab pathBuf PathMax

aw size 2

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Grab path from arg and ensure it is absolute
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp showUsage

mov r1 r0
mov r0 pathBuf
call getabspath

; Get and print file size
mov r0 SyscallIdGetFileLen32
mov r1 pathBuf
mov r2 size
syscall

mov r0 size
call int32MemPrint

mov r0 '\n'
call putc0

; Exit
label done
mov r0 0
call exit

; Usage
label showUsage
mov r0 usageStr
call puts0
jmp done
