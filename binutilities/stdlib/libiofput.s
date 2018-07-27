ab libiofputScratchByte 1

; puts0(strAddr=r0)=puts(0, strAddr)
label puts0
mov r1 r0
mov r0 0
jmp puts

; puts(offset=r0, strAddr=r1)=fputs(stdio, strAddr)
label puts
mov r2 r1
mov r1 r0
mov r0 512 ; Grab stdio fd and put it in r0
syscall
jmp fputs ; TODO: Is this safe allowing a different function to return? (or should it be rather)

; fputs(fd=r0, offset=r1, strAddr=r2)
label fputs

mov r4 0 ; loop index
mov r5 r2 ; copy str base addr into r5

label fputsLoopStart

; load character
mov r2 r5
add r2 r2 r4
load8 r2 r2

; reached null terminator?
cmp r3 r2 r2
skipneqz r3
jmp fputsDone

; print character
push r0
push r1
push r4
push r5
call fputc
pop r5
pop r4
pop r1
pop r0

inc r1
inc r4
jmp fputsLoopStart

label fputsDone
ret

; putc0(c=r0)=putc(0, c)
label putc0
mov r1 r0
mov r0 0
jmp putc

; putc(offset=r0, c=r1)=fputc(stdio, offset, c)
label putc
mov r2 r1
mov r1 r0
mov r0 512
syscall
jmp fputc

; fputc0(fd=r0, c=r1)=fputc(fd, 0, c)
label fputc0
mov r2 r1
mov r1 0
jmp fputc

; fputc(fd=r0, offset=r1, c=r2)
label fputc

; store given character into scratch byte for write call to access
mov r3 libiofputScratchByte
store8 r3 r2

; syscall write
mov r2 r1
mov r1 r0
mov r0 257
mov r3 libiofputScratchByte
mov r4 1
syscall

ret
