require lib/sys/sys.s

requireend lib/pin/pinopen.s
requireend lib/pin/pinsetmode.s
requireend lib/std/proc/exit.s

db blinkFastOnByte 1
db blinkFastOffByte 0

ab blinkFastPinFd 1

; jump past suicide handler (which must exist in first 256 bytes)
jmp start

; suicide handler - close pin fd and exit
label suicideHandler
mov r0 SyscallIdClose
mov r1 blinkFastPinFd
load8 r1 r1
syscall
mov r0 1
call exit

; progra start
label start

; open led pin device file
mov r0 PinLed
call pinopen
cmp r1 r0 r0
skipneqz r1
jmp error

; save fd
mov r1 blinkFastPinFd
store8 r1 r0

; Register suicide signal handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; set pin to output
mov r0 blinkFastPinFd
load8 r0 r0
mov r1 PinModeOutput
call pinsetmode

; prepare for loop
mov r1 blinkFastPinFd
load8 r1 r1 ; pin fd
mov r2 0 ; data offset - ignored for pin device files
mov r4 1 ; data len - single character

; loop start
label loopstart

; Turn LED off
mov r0 SyscallIdWrite
mov r3 blinkFastOffByte
syscall

; Turn LED on
mov r0 SyscallIdWrite
mov r3 blinkFastOnByte
syscall

; Loop back around
jmp loopstart

label error
mov r0 1
call exit
