require strlen.s

; strtrimnewline(r0=str addr) - trims newline from end (if exists)
label strtrimnewline
; Find addr of last char
push16 r0
call strlen
mov r1 r0
pop16 r0

add r2 r0 r1
dec r2

; Insert null byte to truncate, but only do so if last character is newline
load8 r3 r2
mov r4 '\n'
cmp r4 r3 r4

mov r3 0
skipneq r4
store8 r2 r3

ret
