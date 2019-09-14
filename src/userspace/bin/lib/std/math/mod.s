; r0=mod(r0=n, r1=d) - remainder of n/d
label mod
div r2 r0 r1
mul r2 r2 r1
sub r0 r0 r2
ret
