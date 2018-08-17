requireend strlen.s

; r0=strrchr(r0=haystack addr, r1=needle char), r0 is addr of last occurence, or 0 on failure
label strrchr
; call strlen so we can start at the end of the string and work backwards
push r0
push r1
call strlen
mov r2 r0
pop r1
pop r0

; empty string?
cmp r3 r2 r2
skipneqz r3
jmp strrchrNotFound

; loop init (loop index in r2)
dec r2
label strrchrLoopStart

; load character
mov r3 r0
add r3 r3 r2
load8 r3 r3 ; character stored in r3 for now

; hit matching character?
cmp r4 r3 r1
skipneq r4
jmp strrchrLoopEnd

; reached start of string?
cmp r4 r2 r2
skipneqz r4
jmp strrchrNotFound

; try previous character
dec r2
jmp strrchrLoopStart

label strrchrLoopEnd
add r0 r0 r2 ; update r0 to point into string
ret

label strrchrNotFound
mov r0 0
ret
