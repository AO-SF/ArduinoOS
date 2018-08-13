requireend lib/pin/pindef.s
requireend lib/pin/pinset.s
requireend lib/std/time/sleep.s

label start

; Turn LED on
mov r0 pinLed
mov r1 1
call pinset

; Delay for 1s
mov r0 1
call sleep

; Turn LED off
mov r0 pinLed
mov r1 0
call pinset

; Delay for 1s
mov r0 1
call sleep

; Loop back around
jmp start
