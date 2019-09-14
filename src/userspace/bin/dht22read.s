require lib/sys/sys.s

requireend lib/dht22/dht22.s
requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/math/mod.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

ab argBuf ArgLenMax

db usageStr 'usage: sensorhwdeviceslot\n',0

; Grab first argument as DHT22 sensor device slot
mov r0 SyscallIdArgvN
mov r1 1
mov r2 argBuf
mov r3 ArgLenMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usage

mov r0 argBuf
call strtoint

; Print temperature
push8 r0
call dht22GetTemperature
push16 r0
mov r1 10
div r0 r0 r1
call putdec
mov r0 '.'
call putc0
pop16 r0
mov r1 10
call mod
call putdec
mov r0 'C'
call putc0
mov r0 ' '
call putc0

; Print humidity
push8 r0
call dht22GetHumidity
push16 r0
mov r1 10
div r0 r0 r1
call putdec
mov r0 '.'
call putc0
pop16 r0
mov r1 10
call mod
call putdec
mov r0 '%'
call putc0
mov r0 '\n'
call putc0

; Exit
label done
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
jmp done
