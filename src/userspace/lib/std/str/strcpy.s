; strcpy(destAddr=r0, srcAddr=r1)
label strcpy

mov r3 0 ; loop index
mov r4 r1 ; copy str base addr into r4

label strcpyLoopStart

; load character
mov r1 r4
add r1 r1 r3
load8 r1 r1 ; character stored in r1 for now

; copy character
mov r2 r0
add r2 r2 r3
store8 r2 r1

; reached null terminator?
cmp r2 r1 r1
skipneqz r2
jmp strcpyDone

inc r3
jmp strcpyLoopStart

label strcpyDone
ret
