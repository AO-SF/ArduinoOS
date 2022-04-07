require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/getpwd.s
requireend lib/std/str/strcpy.s
requireend lib/std/str/strlen.s

ab queryDir PathMax
ab queryDirFd 1
ab queryDirLen 1

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Check for argument, otherwise use pwd
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp noArg

mov r1 r0
mov r0 queryDir
call getabspath
jmp gotArg

label noArg
call getpwd
mov r1 r0
mov r0 queryDir
call strcpy
label gotArg

; Attempt to open queryDir
mov r0 SyscallIdOpen
mov r1 queryDir
mov r2 FdModeRO
syscall

mov r1 queryDirFd
store8 r1 r0

; Check for bad fd
mov r0 queryDirFd
load8 r0 r0
cmp r0 r0 r0
skipneqz r0
jmp error

; Read path back from fd (to handle user passing relative paths for example)
mov r0 SyscallIdGetPath
mov r1 queryDirFd
load8 r1 r1
mov r2 queryDir
syscall

; Find queryDir length
mov r0 queryDir
call strlen
mov r1 queryDirLen
store8 r1 r0

; Loop calling getChildN and printing results
mov r2 0
push8 r2

label loopStart
mov r0 SyscallIdDirGetChildN
mov r1 queryDirFd
load8 r1 r1
pop8 r2
mov r3 queryDir
syscall
inc r2

; End of children?
cmp r0 r0 r0
skipneqz r0
jmp success

push8 r2

; Strip queryDir from front of child path
mov r0 queryDir
mov r1 queryDirLen
load8 r1 r1
add r0 r0 r1

mov r2 1
cmp r2 r1 r2
skiple r2
inc r0

; Print child path followed by a space
call puts0
mov r0 ' '
call putc0

; Loop again to look for another child
jmp loopStart

; Success - terminate list with newline
label success
mov r0 '\n'
call putc0

; Exit (queryDir closed by OS)
mov r0 0
call exit

; Error
label error
mov r0 1
call exit
