require strlen.s
require ../mem/memmove.s

; strcpy(destAddr=r0, srcAddr=r1)
label strcpy
; protect str addresses
push16 r0
push16 r1
; call strlen on src string
mov r0 r1
call strlen
; use memmove to do copy loop
mov r2 r0
inc r2 ; increment length to include null terminator
pop16 r1
pop16 r0
call memmove
ret
