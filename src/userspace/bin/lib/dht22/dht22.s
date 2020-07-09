require ../sys/sys.s

; These functions assume the DHT22 device is already registered at the given HW device slot

; dht22GetTemperature (r0=hw device slot, returns temperature in degrees C times 10, as an integer in r0)
label dht22GetTemperature
mov r1 r0
mov r0 SyscallIdHwDeviceDht22GetTemperature
syscall
ret

; dht22GetHumidity (r0=hw device slot, returns humidity percentage times 10, as an integer in r0)
label dht22GetHumidity
mov r1 r0
mov r0 SyscallIdHwDeviceDht22GetHumidity
syscall
ret
