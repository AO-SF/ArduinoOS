require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s

db typeStrUnused 'unused',0
db typeStrRaw 'raw',0
db typeStrSdCardReader 'SD card reader',0
db typeStrDht22 'DHT22 Sensor',0
db typeStrAtWinc1500 'ATWINC1500 wireless module',0
db typeStrUnknown 'unknown',0

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop over all devices
mov r1 0
label loopstart

; Print id
push8 r1

mov r0 r1
call putdec

mov r0 ' '
call putc0

pop8 r1

; Get type and print associated string
push8 r1
mov r0 SyscallIdHwDeviceGetType
syscall

mov r1 SyscallHwDeviceTypeUnused
cmp r1 r0 r1
skipneq r1
jmp printTypeUnused

mov r1 SyscallHwDeviceTypeRaw
cmp r1 r0 r1
skipneq r1
jmp printTypeRaw

mov r1 SyscallHwDeviceTypeSdCardReader
cmp r1 r0 r1
skipneq r1
jmp printTypeSdCardReader

mov r1 SyscallHwDeviceTypeDht22
cmp r1 r0 r1
skipneq r1
jmp printTypeDht22

mov r1 SyscallHwDeviceTypeAtWinc1500
cmp r1 r0 r1
skipneq r1
jmp printTypeAtWinc1500

mov r0 typeStrUnknown
call puts0
jmp printTypeEnd

label printTypeUnused
mov r0 typeStrUnused
call puts0
jmp printTypeEnd

label printTypeRaw
mov r0 typeStrRaw
call puts0
jmp printTypeEnd

label printTypeSdCardReader
mov r0 typeStrSdCardReader
call puts0
jmp printTypeEnd

label printTypeDht22
mov r0 typeStrDht22
call puts0
jmp printTypeEnd

label printTypeAtWinc1500
mov r0 typeStrAtWinc1500
call puts0
jmp printTypeEnd

label printTypeEnd

; Print newline
mov r0 '\n'
call putc0

; Increment device id and check for max
pop8 r1
inc r1
mov r4 SyscallHwDeviceIdMax
cmp r4 r1 r4
skiplt r4
jmp loopend

; Jump back to start of loop
jmp loopstart
label loopend

; Exit
mov r0 0
call exit
