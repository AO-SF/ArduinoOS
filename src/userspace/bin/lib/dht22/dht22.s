require ../sys/sys.s

; dht22Register (r0=hw device slot, returns 1/0 for success/failure in r0)
label dht22Register
mov r1 r0
mov r2 3 ; type - DHT22 sensor
mov r0 SyscallIdHwDeviceRegister
syscall
ret

; dht22Deregister (r0=hw device slot)
label dht22Deregister
mov r1 r0
mov r0 SyscallIdHwDeviceDeregister
syscall
ret

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
