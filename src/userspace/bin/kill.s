require lib/sys/sys.s

requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

; Grab pid from first argument
mov r0 SyscallIdArgvN
mov r1 1
mov r3 64
syscall

cmp r1 r0 r0
skipneqz r1
jmp done

; Convert to integer
call strtoint

; Kill
mov r1 r0
mov r0 SyscallIdKill
syscall

; Exit
label done
mov r0 0
call exit
