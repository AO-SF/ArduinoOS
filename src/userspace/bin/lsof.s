require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s

ab pid 1
ab pidPath PathMax
ab localFd 1
ab fdPath PathMax

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Init process loop
mov r0 pid
mov r1 0
store8 r0 r1

; Process loop start
label processLoopStart

; Load pid path and check if even exists
mov r0 SyscallIdGetPidPath
mov r1 pid
load8 r1 r1
mov r2 pidPath
syscall

cmp r0 r0 r0
skipneqz r0
jmp processLoopContinue

; Init local fd loop
mov r0 localFd
mov r1 0
store8 r0 r1

; Local fd loop start
label localFdLoopStart

; Check if local fd is in use
mov r0 SyscallIdGetPidFdN
mov r1 pid
load8 r1 r1
mov r2 localFd
load8 r2 r2
syscall

cmp r1 r0 r0
skipneqz r1
jmp localFdLoopContinue

; It is in use - grab path
mov r1 r0
mov r0 SyscallIdGetPathGlobal
mov r2 fdPath
syscall

cmp r0 r0 r0
skipneqz r0
jmp localFdLoopContinue

; Print pid, pid path, local fd, global fd and fd path
push8 r1 ; protect global fd
mov r0 pid
load8 r0 r0
mov r1 2
call putdecpad
mov r0 ' '
call putc0
mov r0 pidPath
call puts0
mov r0 ' '
call putc0
mov r0 localFd
load8 r0 r0
mov r1 2
call putdecpad
mov r0 ' '
call putc0
pop8 r0 ; grab global fd from stack
mov r1 2
call putdecpad
mov r0 ' '
call putc0
mov r0 fdPath
call puts0
mov r0 '\n'
call putc0

label localFdLoopContinue
; Increment local fd
mov r0 localFd
load8 r1 r0
inc r1
store8 r0 r1

; End of fd indices?
mov r2 MaxFds
cmp r2 r1 r2
skiplt r2
jmp localFdLoopEnd

; Loop to try next local fd
jmp localFdLoopStart
label localFdLoopEnd

label processLoopContinue
; Increment pid loop variable
mov r0 pid
load8 r1 r0
inc r1
store8 r0 r1

; End of processes?
mov r2 PidMax
cmp r2 r1 r2
skiplt r2
jmp processLoopEnd

; Loop to try next process
jmp processLoopStart
label processLoopEnd

; exit
mov r0 0
call exit
