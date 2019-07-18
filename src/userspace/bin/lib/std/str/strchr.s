; r0=strchr(r0=haystack addr, r1=needle char), r0 is addr of first occurence, or 0 on failure
label strchr
; simply use syscall as kernel space is much faster
mov r2 r1
mov r1 r0
mov r0 1536
syscall
ret
