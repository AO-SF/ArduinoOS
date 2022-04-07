requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s

requireend lib/std/int32/int32add.s
requireend lib/std/int32/int32cmp.s
requireend lib/std/int32/int32fput.s
requireend lib/std/int32/int32set.s

db commaSpaceStr ', ',0

aw lower 2
aw upper 2
aw scratch 2

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Init sequence
mov r0 lower
mov r1 0
call int32set16

mov r0 upper
mov r1 1
call int32set16

label loopstart
; Print lowest of two values
mov r0 lower
call int32put0

; Time to end?
mov r0 lower
mov r1 Int32Const2E9
call int32LessThan
skip0 r0
jmp loopend

; Print comma and space
mov r0 commaSpaceStr
call puts0

; Perform single Fibonacci step
; Add upper to lower, then swap
mov r0 lower
mov r1 upper
call int32add32

mov r0 scratch
mov r1 lower
call int32set32
mov r0 lower
mov r1 upper
call int32set32
mov r0 upper
mov r1 scratch
call int32set32

jmp loopstart

; Print newline to terminate list
label loopend
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
