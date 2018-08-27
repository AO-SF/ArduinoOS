; r0=strlen(r0=str addr)
label strlen
mov r1 r0 ; str
mov r0 0 ; length
label strlenLoopStart
; reached null terminator?
load8 r2 r1
cmp r2 r2 r2
skipneqz r2
jmp strlenLoopEnd
; try next character
inc r0
inc r1
jmp strlenLoopStart
label strlenLoopEnd
ret
