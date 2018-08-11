const levelW 64
const levelH 16
const levelWH 1024 ; levelW*levelH
ab levelArray 1024 ; levelWH

label levelLoadCell ; x=r0, y=r1, result returned in r0
mov r2 levelArray
add r0 r0 r2
mov r2 levelW
mul r1 r1 r2
add r0 r0 r1
load8 r0 r0
ret
