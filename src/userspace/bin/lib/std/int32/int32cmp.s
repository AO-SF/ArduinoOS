; int32Equal(opA=r0, opB=r1) - returns 1/0 in r0
label int32Equal
; Compare upper halves
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipeq r4
jmp int32EqualFalse
; Compare lower halves
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipeq r4
jmp int32EqualFalse
; Equal
mov r1 1
ret
label int32EqualFalse
mov r0 0
ret

; int32LessThan(opA=r0, opB=r1) - opA<opB? returns 1/0 in r0
label int32LessThan
; Compare upper halves
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipge r4
jmp int32LessThanFalse
; Compare lower halves
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipge r4
jmp int32LessThanFalse
; Equal
mov r1 1
ret
label int32LessThanFalse
mov r0 0
ret

; int32LessEqual(opA=r0, opB=r1) - opA<=opB? returns 1/0 in r0
label int32LessEqual
; Compare upper halves
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipgt r4
jmp int32LessEqualFalse
; Compare lower halves
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skipgt r4
jmp int32LessEqualFalse
; Equal
mov r1 1
ret
label int32LessEqualFalse
mov r0 0
ret
