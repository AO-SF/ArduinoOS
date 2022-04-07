require fput.s
require ../str/inttostr.s

; putdecpad(x=r0, padLen=r1) - returns number of bytes written in r0
label putdecpad
mov r2 r1
mov r1 r0
mov r0 FdStdout
jmp fputdeccommon

; putdec(x=r0) - returns number of bytes written in r0
label putdec
mov r1 r0
mov r0 FdStdout
jmp fputdec

; fputdec(fd=r0, x=r1) - returns number of bytes written in r0
label fputdec
mov r2 0
jmp fputdeccommon

; fputdeccommon(fd=r0, x=r1, padLen=r2) - write x in ascii as a decimal value to given fd, optionally padded with zeros to given length, returns number of bytes written in r0
label fputdeccommon
; reserve 6 bytes of stack to store temporary string
mov r3 r6
inc6 r6
; convert x to a string
push8 r0 ; protect fd
push8 r2 ; protect padLen
push16 r3 ; protect str addr
mov r0 r3
call inttostr
; print string
pop16 r2 ; restore str addr
pop8 r1 ; restore padLen
mov r3 5
sub r3 r3 r1
cmp r1 r1 r1
skipeqz r1 ; if padLen=0 then no padding
add r2 r2 r3
mov r1 0
pop8 r0 ; restore fd
call fputs
; restore stack
dec6 r6
ret

; putdecsigned(x=r0) - signed 16 bit version of putdec
label putdecsigned
; Positive (upper bit 0)?
mov r1 32768
and r1 r0 r1
cmp r1 r1 r1
skipneqz r1
jmp putdec; this will return from this function as well
; Negative - print minus sign
push16 r0
mov r0 '-'
call putc0
pop16 r0
; Invert number
not r0 r0
inc r0
; Use standard putdec to print remaining positive value
jmp putdec
