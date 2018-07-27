require libstrstrlen.s

; strtrimlast(r0=str addr) - trims last character
label strtrimlast

; Find addr of last char
push r0
call strlen
mov r1 r0
pop r0

add r2 r0 r1
dec r2

; Insert null byte to truncate
mov r3 0
store8 r2 r3

ret
