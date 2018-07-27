; r0=strlen(r0=str addr)
label strlen

mov r2 0 ; loop index

label strlenLoopStart

; load character
mov r3 r0
add r3 r3 r2
load8 r3 r3 ; character stored in r3 for now

; reached null terminator?
cmp r4 r3 r3
skipneqz r4
jmp strlenLoopEnd

; try next character
inc r2
jmp strlenLoopStart

label strlenLoopEnd
mov r0 r2 ; move index into return register
ret
