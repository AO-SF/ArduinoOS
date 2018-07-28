ab libiofgetScratchByte 1

; fgets(fd=r0, offset=r1, buf=r2, len=r3) reads up to and including first newline, always null-terminates buf (potentially to be 0 length if could not read)
; TODO: Support length limit from r3 (using r3 as scratch currently)
label fgets

mov r4 0 ; // loop index

label fgetsLoopStart

; Call fgetc
push r0
push r1
push r2
push r4
call fgetc
mov r5 r0 ; r5 contains read character
pop r4
pop r2
pop r1
pop r0

; Check for failure
mov r3 256
cmp r3 r5 r3
skipneq r3
jmp fgetsLoopEnd

; Copy character
mov r3 r2
add r3 r3 r4
store8 r3 r5
inc r4
inc r1

; Hit newline?
mov r3 '\n'
cmp r3 r5 r3
skipneq r3
jmp fgetsLoopEnd

; Loop around
jmp fgetsLoopStart

label fgetsLoopEnd

; Add null terminator
mov r3 r2
add r3 r3 r4
mov r5 0
store8 r3 r5

ret

; r0=fgetc(fd=r0, offset=r1), returns 256 on failure
label fgetc

; read single character into libiofgetScratchByte
mov r2 r1
mov r1 r0
mov r0 256
mov r3 libiofgetScratchByte
mov r4 1
syscall

; check for failure
cmp r0 r0 r0
skipeqz r0
jmp fgetcDone

; failed
mov r0 256
ret

label fgetcDone
mov r0 libiofgetScratchByte
load8 r0 r0
ret
