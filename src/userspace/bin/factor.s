require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db separator ': ', 0
ab argBuf ArgLenMax

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push8 r0
call factorArgN ; this will exit for us if no such argument
pop8 r0
inc r0
jmp loopstart

; Exit
label error
mov r0 0
call exit

label factorArgN

; Get arg
mov r1 r0
mov r0 SyscallIdArgvN
mov r2 argBuf
mov r3 ArgLenMax
syscall

; No arg found?
cmp r0 r0 r0
skipneqz r0
jmp error

; Convert arg to integer
mov r0 argBuf
call strtoint

; Bad string or 0?
cmp r1 r0 r0
skipneqz r1
jmp factorArgNRet

; Print input and separator
push16 r0
call putdec
mov r0 separator
call puts0
pop16 r0

; Trial division loop
mov r4 2 ; // trial divisor
label factorArgNLoopStart
; Number less than divisor? (or 1 initially)
cmp r1 r0 r4
skipge r1
jmp factorArgNLoopEnd

; Trial division
div r1 r0 r4
mul r2 r1 r4
cmp r2 r2 r0
skipeq r2
jmp factorArgNLoopNext

; We have found a factor, print it
push16 r0
push16 r4
mov r0 r4
call putdec
mov r0 ' '
call putc0
pop16 r4
pop16 r0

; Divide number and decrement divisor so next time we try the same one again
div r0 r0 r4
dec r4

; Try next divisor
label factorArgNLoopNext
inc r4
jmp factorArgNLoopStart
label factorArgNLoopEnd

; Print newline
mov r0 '\n'
call putc0

label factorArgNRet
ret
