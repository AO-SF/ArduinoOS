require ../sys/sys.s

requireend ../pin/pin.s

db spiDevPath '/dev/spi',0

ab spiFd 1
ab spiSlaveSelect 1

; spiSlaveSelectInit (r0=pinNum)
label spiSlaveSelectInit
; Set pin as output
push8 r0
mov r1 PinModeOutput
call pinset
pop8 r0
; Ensure slave is disabled
call spiSlaveSelectDisable
ret

; spiSlaveSelectEnable (r0=pinNum)
label spiSlaveSelectEnable
; Set pin low
mov r1 0
call pinset
ret

; spiSlaveSelectDisable (r0=pinNum)
label spiSlaveSelectDisable
; Set pin high
mov r1 1
call pinset
ret

; spiOpen (r0=slaveSelectPin, returns 1/0 for success/failure in r0)
label spiOpen
; Save slaveSelectPin
mov r1 spiSlaveSelect
store8 r1 r0
; Attempt to open '/dev/spi'
mov r0 SyscallIdOpen
mov r1 spiDevPath
mov r2 FdModeRW
syscall
mov r1 spiFd
store8 r1 r0
cmp r0 r0 r0
skipneqz r0
jmp spiOpenFailure
; Enable slave
mov r0 spiSlaveSelect
load8 r0 r0
call spiSlaveSelectEnable
; Indicate success
mov r0 1
ret
; Indicate failure
label spiOpenFailure
mov r0 0
ret

; spiClose
label spiClose
; Disable slave
mov r0 spiSlaveSelect
load8 r0 r0
call spiSlaveSelectDisable
; Close '/dev/spi'
mov r0 SyscallIdClose
mov r1 spiFd
load8 r1 r1
syscall
ret

; spiReadByte (returns read byte in r0, or 256 if failed to read)
label spiReadByte
; Prepare for read syscall
mov r0 SyscallIdRead
mov r1 spiFd
load8 r1 r1
mov r2 0 ; offset is ignored anyway
mov r3 r6 ; use stack to store returned byte
mov r4 1; data len
syscall
; Bad read?
cmp r0 r0 r0
skipneqz r0
jmp spiReadByteBadRead
; Grab read value
load8 r0 r6
ret
; Error case
label spiReadByteBadRead
mov r0 256
ret

; spiWriteByte (r0=value, returns 1/0 in r0 for success/failure)
label spiWriteByte
; Push given value onto stack so we have a ptr to it
push8 r0
; Prepare for write syscall
mov r0 SyscallIdWrite
mov r1 spiFd
load8 r1 r1
mov r2 0 ; offset is ignored anyway
mov r3 r6 ; data
dec r3
mov r4 1; data len
syscall
; Restore stack
dec r6
ret ; note: r0 still contains 1 or 0 due to write syscall, so we can simply return it unchanged
