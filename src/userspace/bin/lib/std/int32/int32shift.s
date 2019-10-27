; int32ShiftLeft16(r0=x)=int32shiftLeft(x, 16) - special case of shift left where shift is fixed at 16 bits
label int32ShiftLeft16
; Move lower into upper, setting lower to 0 in the process
inc2 r0
load16 r1 r0
mov r2 0
store16 r0 r2
dec2 r0
store16 r0 r1
ret
