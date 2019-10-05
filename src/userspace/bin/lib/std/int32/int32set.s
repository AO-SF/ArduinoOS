; int32set16(dest=r0, src=r1) - 32 bit dest ptr = 16 bit src value
label int32set16
mov r2 0
store16 r0 r2
inc2 r0
store16 r0 r1
ret

; int32setUpper16(dest=r0, src=r1) - 32 bit dest ptr = (16 bit src value)<<16
label int32setUpper16
store16 r0 r1
inc2 r0
mov r2 0
store16 r0 r2
ret

; int32set16shift(dest=r0, src=r1, shift=r2) - like int32set16 but also shifts src first
label int32set16shift
mov r3 16
sub r3 r3 r2
shr r3 r1 r3
store16 r0 r3
inc2 r0
shl r3 r1 r2
store16 r0 r3
ret

