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
mov r0 1
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
skiple r4
jmp int32LessThanFalse ; upper half of opA is greater than upper half of opB, thus opA>opB
skipeq r4
jmp int32LessThanTrue ; upper half of opA is less than upper half of opB, thus opA<opB
; Upper halves equal - compare lower halves
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skiplt r4
jmp int32LessThanFalse
; Equal
label int32LessThanTrue
mov r0 1
ret
; False case
label int32LessThanFalse
mov r0 0
ret

; int32LessEqual(opA=r0, opB=r1) - opA<=opB? returns 1/0 in r0
label int32LessEqual
; Compare upper halves
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skiple r4
jmp int32LessEqualFalse ; upper half of opA is greater than upper half of opB, thus opA>opB
skipeq r4
jmp int32LessEqualTrue ; upper half of opA is less than upper half of opB, thus opA<opB
; Upper halves equal - compare lower halves
inc2 r0
inc2 r1
load16 r2 r0
load16 r3 r1
cmp r4 r2 r3
skiple r4
jmp int32LessEqualFalse
; Equal
label int32LessEqualTrue
mov r0 1
ret
; False case
label int32LessEqualFalse
mov r0 0
ret
