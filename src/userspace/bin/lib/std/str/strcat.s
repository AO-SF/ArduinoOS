require strcpy.s
require strlen.s

; strcat(r0=dest, r1=src)
label strcat

; Find length of dest
push16 r0
push16 r1
call strlen
mov r2 r0 ; length in r2
pop16 r1
pop16 r0

; Copy src to end of dest
add r0 r0 r2 ; r0 starts with dest, ends with dest+len
call strcpy ; r1 already has src

ret
