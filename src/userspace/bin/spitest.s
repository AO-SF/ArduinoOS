require lib/sys/sys.s

requireend lib/spi/spi.s
requireend lib/std/proc/exit.s
requireend lib/std/time/sleep.s

; Note: technically we should set this pin to mode output and state low
; However as we take so long to boot, this has to happen in the kernel before booting.
; (via pinSetMode and pinWrite)
; Otherwise the slave can be connected later once SS pin has been setup in user space.
const SlaveSelectPin PinD53

ab counter 1

; Set counter
mov r0 counter
mov r1 ' '
store8 r0 r1

; Start of message printing loop
label loopstart

; Attempt to open SPI interface
mov r0 SlaveSelectPin
call spiOpen
cmp r0 r0 r0
skipneqz r0
jmp loopdelay ; failure

; Write string
mov r0 'D'
call spiWriteByte
mov r0 'J'
call spiWriteByte
mov r0 'W'
call spiWriteByte
mov r1 counter
load8 r0 r1
inc r0
store8 r1 r0
call spiWriteByte
mov r0 '\n'
call spiWriteByte

; Close SPI interface
call spiClose

; Delay for 1s
label loopdelay
mov r0 1
call sleep

; Infinite loop
jmp loopstart
