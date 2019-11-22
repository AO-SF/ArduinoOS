require int32common.s

; int32SwapEndianness(x=r0) - swap order of bytes (so swap bytes 0 and 3, and swap bytes 1 and 2)
label int32SwapEndianness
; Swap bytes 0 and 3
load8 r1 r0
inc3 r0
xchg8 r0 r1
dec3 r0
store8 r0 r1
; Swap bytes 1 and 2
inc r0
load8 r1 r0
inc r0
xchg8 r0 r1
dec r0
store8 r0 r1
ret
