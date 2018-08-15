; startup program - called by init before dropping into a shell

db devicePath '/dev/eeprom',0
db dirPath '/home',0

; mount eeprom as home directory
mov r0 1281
mov r1 0 ; minifs volume
mov r2 devicePath
mov r3 dirPath
syscall

; exit
mov r0 0
mov r1 0
syscall
