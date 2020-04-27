require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

; First get current log level
mov r0 SyscallIdGetLogLevel
syscall
call putdec
mov r0 '\n'
call putc0

; Check arg
mov r0 SyscallIdArgvN
mov r1 1
syscall

; No arg?
cmp r1 r0 r0
skipneqz r1
jmp done

; Convert arg to integer
call strtoint

; Set log level
mov r1 r0
mov r0 SyscallIdSetLogLevel
syscall

; Exit
label done
mov r0 0
call exit
