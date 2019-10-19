require int32common.s

requireend int32cmp.s

; array of precomputed values for 10^n, each pair using 4 bytes and representing one entry
dw Int32Exp10Array 0,1,0,10,0,100,0,1000,0,10000,1,34464,15,16960,152,38528,1525,57600,15258,51712

; int32exp10(n=r0) - computes 10^n where n is a standard 16 bit value in the range [0,9], and places a const pointer to the result in r0. it is an error if n is not in the given range
label int32exp10
; multiply n by 4 as each entry in our array uses 4 bytes
mov r1 4
mul r0 r0 r1
; calculate pointer into array
mov r1 Int32Exp10Array
add r0 r0 r1
ret
