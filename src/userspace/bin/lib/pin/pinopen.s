require ../sys/sys.s

requireend ../std/str/inttostr.s
requireend ../std/str/strcpy.s

db pinopenPrefix '/dev/pin',0
const pinopenPrefixLen 8

label pinopen ; num=r0, returns fd in r0 (or 0 on failure)
; Copy prefix onto stack
mov r1 r6 ; save PathMax bytes on the stack, ptr to this space in r1 for now
mov r6 PathMax
add r6 r1 r6
push8 r0 ; save passed pin number to stack
mov r0 r1 ; move ptr to our stack space ready for strcpy call
mov r1 pinopenPrefix
call strcpy
pop8 r1 ; pop passed pin number off stack
; Append given pin num to path buf
mov r0 PathMax
sub r0 r6 r0
mov r2 pinopenPrefixLen
add r0 r0 r2
mov r2 0 ; no padding
call inttostr
; open file at calculated path
mov r0 SyscallIdOpen
mov r1 PathMax
sub r6 r6 r1 ; restore stack
mov r1 r6
syscall
ret
