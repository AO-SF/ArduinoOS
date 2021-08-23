require ../../sys/syscall.s

; memchr(haystackaddr=r0, needlechar=r1, size=r2) - returns pointer to first occurence of needlechar in haystack, or 0 if not found
label memchr
; simply use syscall
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdMemChr
syscall
ret
