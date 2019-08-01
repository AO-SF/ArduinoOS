; r=fgets(fd=r0, offset=r1, buf=r2, len=r3) reads up to and including first newline, always null-terminates buf (potentially to be 0 length if could not read), returns number of bytes read from file
; TODO: Support length limit from r3 (using r3 as scratch currently)
label fgets

mov r4 0 ; // loop index

label fgetsLoopStart

; Call fgetc
push8 r0
push16 r1
push16 r2
push16 r4
call fgetc
mov r5 r0 ; r5 contains read character
pop16 r4
pop16 r2
pop16 r1
pop8 r0

; Check for failure
mov r3 256
cmp r3 r5 r3
skipneq r3
jmp fgetsLoopEnd

; Check for backspace
mov r3 127
cmp r3 r5 r3
skipeq r3
jmp fgetsLoopNoBackspace
; found a backspace
; decrement r4 instead to delete the last char, unless we are already at the start
cmp r3 r4 r4
skipeqz r3
dec r4
jmp fgetsLoopStart
label fgetsLoopNoBackspace

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

mov r0 r4 ; return length

ret

; r0=fgetc(fd=r0, offset=r1), returns 256 on failure
label fgetc
; read single character onto stack
mov r2 r1
mov r1 r0
mov r0 SyscallIdRead
mov r3 r6
inc r6
mov r4 1
syscall
; check for failure
cmp r0 r0 r0
skipeqz r0
jmp fgetcDone
; failed
dec r6 ; fix stack
mov r0 256
ret
; success
label fgetcDone
pop8 r0 ; pop read character off stack
ret
