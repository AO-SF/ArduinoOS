requireend ../io/fput.s

requireend int32get.s
requireend int32str.s

; int32put0(x=r0) - equivalent to int32fput0(x, stdout)
label int32put0
; Protect x
mov r2 r0
; Grab stdout fd
mov r0 SyscallIdEnvGetStdoutFd ; Grab stdout fd and put it in r0
syscall
; Jump into fput0 version to do rest of the work
mov r1 r0 ; set fd
mov r0 r2 ; set x
jmp int32fput0

; int32fput0(x=r0, fd=r1) - equivalent to int32fput(x, fd, 0)
label int32fput0
mov r2 0
jmp int32fput

; int32fput(x=r0, fd=r1, offset=r2) - write 32 bit value pointed to by x into given fd at given offset
label int32fput
; Reserve space on stack for string
mov r3 r6 ; r3 contains ptr to temporary str
mov r4 int32toStrBufSize
add r6 r6 r4
; Convert x to string
push8 r1
push16 r2
push16 r3
mov r1 r0
mov r0 r3
call int32toStr
; Write string to file
pop16 r2
pop16 r1
pop8 r0
call fputs
; Restore stack
mov r4 int32toStrBufSize
sub r6 r6 r4
ret

; int32debug(x=r0) - prints 32 bit integer pointed to by x in the form: "{upper,lower}", with upper and lower in decimal.
label int32debug
push16 r0
mov r0 '{'
call putc0
pop16 r0
push16 r0
call int32getUpper16
call putdec
mov r0 ','
call putc0
pop16 r0
call int32getLower16
call putdec
mov r0 '}'
call putc0
ret
