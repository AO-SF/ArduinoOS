; inttostr(str=r0, val=r1)
label inttostr
; Init ready for loop
mov r2 10000
mov r4 0 ; foundDigit flag
; Divide to find current digit
label inttostrLoopStart
div r3 r1 r2 ; current digit in r3
; If this digit is non-zero we need to add it
cmp r5 r3 r3
skipeqz r5
jmp inttostrLoopAdd
; If we have already found a significant digit we need to add this one
cmp r5 r4 r4
skipeqz r5
jmp inttostrLoopAdd
; If this is the last digit we also need to add it regardless
mov r5 1
cmp r5 r2 r5
skipneq r5
jmp inttostrLoopAdd
; Otherwise no need to add
jmp inttostrLoopNext
; Add digit
label inttostrLoopAdd
mov r4 '0'
add r4 r4 r3
store8 r0 r4
inc r0
mov r4 1 ; set foundDigit flag
; Take digits value away from x
label inttostrLoopNext
mul r3 r3 r2
sub r1 r1 r3
; Finished? (factor=1)
mov r3 1
cmp r3 r2 r3
skipneq r3
jmp inttostrLoopEnd
; Reduce divisor by factor of 10
mov r3 10
div r2 r2 r3
; Loop to add next digit
jmp inttostrLoopStart
label inttostrLoopEnd
; Add null terminator
mov r4 0
store8 r0 r4
ret
