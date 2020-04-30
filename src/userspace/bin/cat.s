require lib/sys/sys.s

requireend lib/std/int32/int32add.s
requireend lib/std/int32/int32set.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/str/strcmp.s

db dashStr '-',0

ab pathBuf PathMax
ab fd 1
aw readOffset 2 ; 32 bit integer

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push8 r0
call catArgN ; this will exit for us if no such argument
pop8 r0
inc r0
jmp loopstart

; Exit
mov r0 0
call exit

label error
mov r0 1
call exit

label catArgN

; Get arg
mov r1 r0
mov r0 SyscallIdArgvN
syscall

; No arg found?
cmp r1 r0 r0
skipneqz r1
jmp error

; Check for dash to mean use stdin
push16 r0
mov r1 dashStr
call strcmp
cmp r1 r0 r0
pop16 r0
skipeqz r1
jmp catArgNSetupFdPath
jmp catArgNSetupFdStdin

; Dash given to mean use stdin
label catArgNSetupFdStdin
; Store stdin fd into fd variable
mov r0 FdStdin
mov r1 fd
store8 r1 r0

jmp catArgNSetupFdCommon

; Path given - make absolute
label catArgNSetupFdPath
mov r1 r0
mov r0 pathBuf
call getabspath

; Open file and store fd
mov r0 SyscallIdOpen
mov r1 pathBuf
mov r2 FdModeRO
syscall

mov r1 fd
store8 r1 r0

jmp catArgNSetupFdCommon

; Check valid fd stored
label catArgNSetupFdCommon
mov r0 fd
load8 r0 r0
cmp r1 r0 r0
skipneqz r1
jmp error

; Read data from file, printing to stdout
mov r0 readOffset
mov r1 0
call int32set16
label catArgNLoopStart

; Read block (reusing pathBuf)
mov r0 SyscallIdRead32
mov r1 fd
load8 r1 r1
mov r2 readOffset
mov r3 pathBuf
mov r4 PathMax
syscall

; Check for EOF
cmp r4 r0 r0
skipneqz r4
jmp catArgNLoopEnd

; Print block
mov r4 r0
mov r1 FdStdout
mov r0 SyscallIdWrite
; we can leave r2 non-zero as offset is ignored for stdout
mov r3 pathBuf
syscall

cmp r0 r0 r0
skipneqz r0
jmp catArgNLoopEnd

; Advance to next block
mov r0 readOffset
mov r1 r4
call int32add16
jmp catArgNLoopStart
label catArgNLoopEnd

; Close file
; Skip this for stdin
mov r0 SyscallIdClose
mov r1 fd
load8 r1 r1
mov r2 FdStdin
cmp r2 r1 r2
skipeq r2
syscall

ret
