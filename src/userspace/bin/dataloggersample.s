; Notes on use as logger
; SD card reader in slot 0 - so pins D42 (power) and D43 (chip select), then SPI bus
; DHT22 sensor in slot 1 - so pins D44 (power) and D45 (data)
; SD card should be formatted as a loop device (no partition table), with an empty and large MiniFs volume

require lib/sys/sys.s

requireend lib/dht22/dht22.s
requireend lib/std/int32/int32time.s
requireend lib/std/io/fflush.s
requireend lib/std/io/fput.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

ab buf ArgLenMax
ab outputPath PathMax

db usageStr 'usage: sensorhwdeviceslot datafile\n',0

; Grab first argument as DHT22 sensor device slot
mov r0 SyscallIdArgvN
mov r1 1
mov r2 buf
mov r3 ArgLenMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

mov r0 buf
call strtoint
push8 r0 ; protect device slot

; Grab data file path argument
mov r0 SyscallIdArgvN
mov r1 2
mov r2 buf
mov r3 ArgLenMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

mov r0 outputPath
mov r1 buf
call getabspath

; Read temperature and humidity values into buffer
pop8 r0 ; restore device slot
push8 r0
call dht22GetTemperature
mov r1 buf
inc4 r1
store16 r1 r0

pop8 r0
call dht22GetHumidity
mov r1 buf
inc6 r1
store16 r1 r0

; Read current real time into buffer
mov r0 buf
call int32gettimereal

; Append data to file
mov r0 outputPath
mov r1 buf
mov r2 8
call fputappend

; Ensure file is flushed so data is not lost if e.g. power lost
mov r0 outputPath
call fflush

; Exit
label done
mov r0 0
call exit

; Usage
label usage
mov r0 usageStr
call puts0
jmp done
