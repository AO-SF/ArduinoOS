requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

ab argBuf 64

; Grab pid from first argument
mov r0 3
mov r1 1
mov r2 argBuf
mov r3 64
syscall

cmp r1 r0 r0
skipneqz r1
jmp done

; Convert to integer
mov r0 argBuf
call strtoint

; Kill
mov r1 r0
mov r0 10
syscall

; Exit
label done
mov r0 0
call exit
