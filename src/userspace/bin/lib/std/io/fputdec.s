require fput.s
require ../str/inttostr.s

; putdecpad(x=r0)
label putdecpad
mov r1 r0
mov r0 SyscallIdEnvGetStdoutFd
syscall
mov r2 1
jmp fputdeccommon

; putdec(x=r0)
label putdec
mov r1 r0
mov r0 SyscallIdEnvGetStdoutFd
syscall
jmp fputdec

; fputdec(fd=r0, x=r1)
label fputdec
mov r2 0
jmp fputdeccommon

; fputdeccommon(fd=r0, x=r1, padFlag=r2) - write x in ascii as a decimal value to given fd, optionally padded with zeros
label fputdeccommon
; reserve 6 bytes of stack to store temporary string
mov r3 r6
inc6 r6
; convert x to a string
push8 r0 ; protect fd
push16 r3 ; protect str addr
mov r0 r3
call inttostr
; print string
pop16 r2 ; restore str addr
mov r1 0
pop8 r0 ; restore fd
call fputs
; restore stack
dec6 r6
ret
