require strcat.s
require strlen.s

db strpadspacestr ' ', 0

; strpadfront(dest=r0, src=r1, len=r2) - copies src to dest, padding with spaces if src is smaller than len
label strpadfront

; Make dest empty string for now
mov r3 0
store8 r0 r3

; Find length of src
push r0
push r1
push r2

mov r0 r1
call strlen
mov r3 r0

pop r2
pop r1
pop r0

; if src is smaller than len, we can pad
cmp r4 r3 r2
skiplt r4
jmp strpadcopy

; append spaces
sub r2 r2 r3 ; r2 now contains number of spaces to append
label strpadspacestart
cmp r3 r2 r2
skipneqz r3
jmp strpadspaceend
push r0
push r1
push r2
mov r1 strpadspacestr
call strcat
pop r2
pop r1
pop r0
dec r2
jmp strpadspacestart
label strpadspaceend

label strpadcopy
call strcat

ret
