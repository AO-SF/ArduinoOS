requireend ../../sys/sys.s
requireend ../str/strlen.s

; puts0(strAddr=r0)=puts(0, strAddr) - returns number of bytes written in r0
label puts0
mov r1 r0
mov r0 0
jmp puts

; puts(offset=r0, strAddr=r1)=fputs(stdio, strAddr) - returns number of bytes written in r0
label puts
mov r2 r1
mov r1 r0
mov r0 FdStdout
jmp fputs

; fputs(fd=r0, offset=r1, strAddr=r2) - returns number of bytes written in r0
; uses strlen so we can do actual write loop in kernel space (via write syscall),
; rather than writing a byte at a time
label fputs
push8 r0
push16 r1
push16 r2
mov r0 r2
call strlen
mov r4 r0 ; string len
mov r0 SyscallIdWrite
pop16 r3 ; str
pop16 r2 ; offset
pop8 r1 ; fd
syscall
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
mov r0 FdStdout
jmp fputc

; fputc0(fd=r0, c=r1)=fputc(fd, 0, c)
label fputc0
mov r2 r1
mov r1 0
jmp fputc

; fputc(fd=r0, offset=r1, c=r2)
label fputc
; store given character on stack for write call to access
mov r3 r6
push8 r2
; syscall write
mov r2 r1
mov r1 r0
mov r0 SyscallIdWrite
mov r4 1
syscall
; restore stack
dec r6
ret

; fputappend(path=r0, data=r1, dataLen=r2) - append given data to file at path, resizes and writes data, returning 1/0 in r0 for success/failure
label fputappend
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdAppend
syscall
ret
