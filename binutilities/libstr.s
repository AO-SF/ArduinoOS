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

; strtrimlast(r0=str addr) - trims last character
label strtrimlast

; Find addr of last char
push r0
call strlen
mov r1 r0
pop r0

add r2 r0 r1
dec r2

; Insert null byte to truncate
mov r3 0
store8 r2 r3

ret

; strcat(r0=dest, r1=src)
label strcat

; Find length of dest
push r0
push r1
call strlen
mov r2 r0 ; length in r2
pop r1
pop r0

; Copy src to end of dest
add r0 r0 r2 ; r0 starts with dest, ends with dest+len
call strcpy ; r1 already has src

ret

; strequal(addr A=r0, addr B=r1)
label strequal

label strequalLoopStart
; load characters
load8 r2 r0 ; 1st char stored into r2
load8 r3 r1 ; 2nd char stored into r3

; check if characters differ
cmp r4 r2 r3
skipeq r4
jmp strequalFalse

; reached null terminator?
cmp r4 r2 r2
skipneqz r4
jmp strequalTrue

; advance to next characters
inc r0
inc r1
jmp strequalLoopStart

label strequalFalse
mov r0 0
ret

label strequalTrue
mov r0 1
ret
