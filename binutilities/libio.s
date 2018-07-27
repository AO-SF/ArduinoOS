ab libIoScratchByte 1

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

; read single character into libIoScratchByte
mov r1 r0
mov r0 256
mov r2 libIoScratchByte
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
mov r0 libIoScratchByte
load8 r0 r0
ret

; puts(strAddr=r0)=fputs(stdio, strAddr)
label puts
mov r1 r0 ; Move string address into r1 ready for fputs call
mov r0 512 ; Grab stdio fd and put it in r0
syscall
jmp fputs ; TODO: Is this safe allowing a different function to return? (or should it be rather)

; fputs(fd=r0, strAddr=r1)
label fputs

mov r3 0 ; loop index
mov r4 r1 ; copy str base addr into r4

label fputsLoopStart

; load character
mov r1 r4
add r1 r1 r3
load8 r1 r1

; reached null terminator?
cmp r2 r1 r1
skipneqz r2
jmp fputsDone

; print character
push r0
push r3
push r4

call fputc

pop r4
pop r3
pop r0

inc r3
jmp fputsLoopStart

label fputsDone
ret

; putc(c=r0)=fpuc(stdio, c)
label putc
mov r1 r0
mov r0 512
syscall
jmp fputc

; fputc(fd=r0, c=r1)
label fputc

; store given character into scratch byte for write call to access
mov r2 libIoScratchByte
store8 r2 r1

; syscall write
mov r1 r0 ; move fd before we overwrite it
mov r0 257
mov r2 libIoScratchByte
mov r3 1
syscall

ret
