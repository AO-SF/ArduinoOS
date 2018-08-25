require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s

db usageErrorStr 'usage: unmount device\n',0

ab scratchBuf PathMax

ab devicePath PathMax

; Read device argument
mov r0 3
mov r1 1
mov r2 scratchBuf
mov r3 PathMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usageerror
mov r0 devicePath
mov r1 scratchBuf
call getpath

; Invoke unmount syscall
mov r0 1282
mov r1 devicePath
syscall

; Exit
mov r0 0
call exit

; Usage error
label usageerror
mov r0 usageErrorStr
call puts0
mov r0 1
call exit
