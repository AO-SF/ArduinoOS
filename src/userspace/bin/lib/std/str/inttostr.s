; inttostr(str=r0, x=r1, padFlag=r2) - write x in ascii as a decimal value to given string, optionally padded with zeros
; note: this was originally done with a loop but it is significantly faster unrolled and optimised
label inttostr
; if padding flag set, simply jump straight to printing first digit
cmp r2 r2 r2
skipeqz r2
jmp inttostrprint4
; check if single digit
mov r2 10
cmp r4 r1 r2
skipge r4
jmp inttostrprint0
; check if two digits
mov r3 100
cmp r4 r1 r3
skipge r4
jmp inttostrprint1
; check if three digits
mul r3 r2 r3 ; shorter version of `mov r3 1000`
cmp r4 r1 r3
skipge r4
jmp inttostrprint2
; check if four digits
mul r2 r2 r3 ; shorter version of `mov r2 10000`
cmp r4 r1 r2
skipge r4
jmp inttostrprint3
; else five digits
; print ten-thousands digit
label inttostrprint4
mov r2 10000
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
inc48 r3 ; add '0' ascii offset
store8 r0 r3
inc r0
; print thousands digit
label inttostrprint3
mov r2 1000
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
inc48 r3 ; add '0' ascii offset
store8 r0 r3
inc r0
; print hundreds digit
label inttostrprint2
mov r2 100
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
inc48 r3 ; add '0' ascii offset
store8 r0 r3
inc r0
; print tens digit
label inttostrprint1
mov r2 10
div r3 r1 r2
mul r4 r3 r2
sub r1 r1 r4
inc48 r3 ; add '0' ascii offset
store8 r0 r3
inc r0
; print units digit
label inttostrprint0
; divisor is one - nothing to do
inc48 r1 ; add '0' ascii offset
store8 r0 r1
inc r0
; add null terminator
mov r2 0
store8 r0 r2
ret
