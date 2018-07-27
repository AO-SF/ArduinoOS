; strequal(addr A=r0, addr B=r1)
label strequal

label strequalLoopStart
; load characters
load8 r2 r0 ; 1st char stored into r2
load8 r3 r1 ; 2nd char stored into r3

; check if characters differ
cmp r4 r2 r3
skipeq r4
jmp strequalFalse

; reached null terminator?
cmp r4 r2 r2
skipneqz r4
jmp strequalTrue

; advance to next characters
inc r0
inc r1
jmp strequalLoopStart

label strequalFalse
mov r0 0
ret

label strequalTrue
mov r0 1
ret
