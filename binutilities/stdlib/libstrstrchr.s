; r0=strchr(r0=haystack addr, r1=needle char), r0 is addr of first occurence, or 0 on failure
label strchr

mov r2 0 ; loop index

label strchrLoopStart

; load character
mov r3 r0
add r3 r3 r2
load8 r3 r3 ; character stored in r3 for now

; hit matching character?
cmp r4 r3 r1
skipneq r4
jmp strchrLoopEnd

; reached null terminator?
cmp r4 r3 r3
skipneqz r4
jmp strchrNotFound

; try next character
inc r2
jmp strchrLoopStart

label strchrLoopEnd
add r0 r0 r2 ; update r0 to point into string
ret

label strchrNotFound
mov r0 0
ret
