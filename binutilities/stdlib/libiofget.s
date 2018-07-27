ab libiofgetScratchByte 1

; fgets(fd=r0, buf=r1, len=r2) reads up to and including first newline, always null-terminates buf (potentially to be 0 length if could not read)
; TODO: Support length limit from r2
label fgets

mov r3 0 ; // loop index

label fgetsLoopStart

; Call fgetc
push r0
push r1
push r3
call fgetc
mov r4 r0 ; r4 contains read character
pop r3
pop r1
pop r0

; Check for failure
mov r5 256
cmp r5 r4 r5
skipneq r5
jmp fgetsLoopEnd

; Copy character
mov r5 r1
add r5 r5 r3
store8 r5 r4
inc r3

; Hit newline?
mov r5 '\n'
cmp r5 r4 r5
skipneq r5
jmp fgetsLoopEnd

; Loop around
jmp fgetsLoopStart

label fgetsLoopEnd

; Add null terminator
mov r5 r1
add r5 r5 r3
mov r4 0
store8 r5 r4

ret

; r0=fgetc(fd=r0), returns 256 on failure
label fgetc

; read single character into libiofgetScratchByte
mov r1 r0
mov r0 256
mov r2 libiofgetScratchByte
mov r3 1
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
