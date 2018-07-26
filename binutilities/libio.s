ab libIoScratchByte 1

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
