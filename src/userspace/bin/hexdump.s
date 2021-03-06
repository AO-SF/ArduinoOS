require lib/sys/sys.s

requireend lib/std/int32/int32add.s
requireend lib/std/int32/int32set.s
requireend lib/std/io/fputhex.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/str/strcmp.s

db dashStr '-',0

ab pathBuf PathMax
ab fd 1

aw fileOffset 2 ; 32 bit int

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push8 r0
call hexDumpArgN ; this will exit for us if no such argument
pop8 r0
inc r0
jmp loopstart

; Exit
mov r0 0
call exit

label error
mov r0 1
call exit

label hexDumpArgN

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
jmp hexDumpArgNSetupFdPath
jmp hexDumpArgNSetupFdStdin

; Setup fd for stdin
label hexDumpArgNSetupFdStdin
mov r0 FdStdin
mov r1 fd
store8 r1 r0

jmp hexDumpArgNSetupFdEnd

; Setup for standard path
label hexDumpArgNSetupFdPath

; Convert to absolute path
mov r1 r0
mov r0 pathBuf
call getabspath

; Open file
mov r0 SyscallIdOpen
mov r1 pathBuf
mov r2 FdModeRO
syscall
mov r1 fd
store8 r1 r0

jmp hexDumpArgNSetupFdEnd

; Common fd code
label hexDumpArgNSetupFdEnd

; Check for bad fd
cmp r1 r0 r0
skipneqz r1
jmp error

; Read data from file, printing to stdout
mov r0 fileOffset
mov r1 0
call int32set16
label hexDumpArgNLoopStart

; Read block (reusing pathBuf)
mov r0 SyscallIdRead32
mov r1 fd
load8 r1 r1
mov r2 fileOffset
mov r3 pathBuf
mov r4 PathMax
syscall ; r0 now contains length of read block

; Check for EOF
cmp r4 r0 r0
skipneqz r4
jmp hexDumpArgNLoopEnd

; Print block (r0 contains length)
; Note: need to protect r0
mov r1 0 ; r1 contains index to pathBuf
label printLoopStart
; print block - load byte
push8 r0
push8 r1
mov r0 pathBuf
add r0 r0 r1
load8 r0 r0
call puthex8
mov r0 ' '
call putc0
pop8 r1
pop8 r0
; print block - end of loop?
inc r1
cmp r4 r1 r0
skipge r4
jmp printLoopStart

; Advance to next block
mov r1 r0
mov r0 fileOffset
call int32add16
jmp hexDumpArgNLoopStart
label hexDumpArgNLoopEnd

; Close file
; Skip this for stdin
mov r0 FdStdin
load8 r1 r1
cmp r2 r0 r1
mov r0 SyscallIdClose
skipeq r2
syscall

; Print newline
mov r0 '\n'
call putc0

ret
