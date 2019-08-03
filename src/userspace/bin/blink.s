require lib/sys/sys.s

requireend lib/pin/pindef.s
requireend lib/pin/pinset.s
requireend lib/std/proc/exit.s
requireend lib/std/time/sleep.s

ab quitFlag 1 ; this exists to ensure we return from pinset calls, and thus do not leave any files open

; Set quit flag to false
mov r0 quitFlag
mov r1 0
store8 r0 r1

; Register suicide signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall
jmp loopstart

; suicide handler - set quitFlag true so we exit very soon
label suicideHandler
mov r0 quitFlag
mov r1 1
store8 r0 r1
ret

; Loop start
label loopstart

; Turn LED on
mov r0 PinLed
mov r1 1
call pinset

; Told to quit?
mov r0 quitFlag
load8 r0 r0
cmp r0 r0 r0
skipeqz r0
jmp done

; Delay for 1s
mov r0 1
call sleep

; Turn LED off
mov r0 PinLed
mov r1 0
call pinset

; Told to quit?
mov r0 quitFlag
load8 r0 r0
cmp r0 r0 r0
skipeqz r0
jmp done

; Delay for 1s
mov r0 1
call sleep

; Loop back to start
jmp loopstart

; Exit
label done
mov r0 0
call exit
