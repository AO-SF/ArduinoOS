require lib/sys/sys.s

requireend lib/pin/pindef.s
requireend lib/pin/pinset.s
requireend lib/std/proc/exit.s
requireend lib/std/time/sleep.s

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop start
label loopstart

; Turn LED on
mov r0 PinLed
mov r1 1
call pinset

; Delay for 1s
mov r0 1
call sleep

; Turn LED off
mov r0 PinLed
mov r1 0
call pinset

; Delay for 1s
mov r0 1
call sleep

; Loop back to start
jmp loopstart

; Exit
label done
mov r0 0
call exit
