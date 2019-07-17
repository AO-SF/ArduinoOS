; r0=strlen(r0=str addr)
label strlen
; protect string for later subtraction
push16 r0
; use strchrnul syscall as the overhead is
; minimal and kernel space is much faster
mov r1 r0
mov r0 1537
mov r2 0 ; search for a null byte, i.e. the end of the string
syscall
; we now have a pointer to the end of the string,
; so subtract the base address to find length
pop16 r1
sub r0 r0 r1
ret
