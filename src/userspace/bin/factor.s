require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db primes 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131,137,139,149,151,157,163,167,173,179,181,191,193,197,199,211,223,227,229,233,239,241,251,0

db separator ': ', 0

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
syscall

; No arg found?
cmp r1 r0 r0
skipneqz r1
jmp error

; Convert arg to integer
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

; Division loop
; r0 contains value to factor
mov r1 primes ; // primes array ptr
label factorArgNLoopStart
; Number reduced to less than 2?
mov r2 2
cmp r2 r0 r2
skipge r2
jmp factorArgNLoopEnd

; Load prime factor
load8 r2 r1
cmp r3 r2 r2
skipneqz r3
jmp factorArgNLoopEndPrint

; Test if prime is a factor
div r3 r0 r2
mul r3 r3 r2
cmp r3 r0 r3
skipeq r3
jmp factorArgNLoopNext

; Factor found - divide number
div r0 r0 r2

; Print factor
push16 r0
push16 r1
mov r0 r2
call putdec
mov r0 ' '
call putc0
pop16 r1
pop16 r0

; Loop to try this divisor again
jmp factorArgNLoopStart

; Try next divisor
label factorArgNLoopNext
inc r1 ; advance to next prime
jmp factorArgNLoopStart

; Print remaining value
label factorArgNLoopEndPrint
call putdec

label factorArgNLoopEnd

; Print newline
mov r0 '\n'
call putc0

; Done
label factorArgNRet
ret
