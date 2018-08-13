; r0=abs(r0)
label abs
mov r2 0
cmp r1 r0 r2
skipge r1
sub r0 r2 r0
ret
