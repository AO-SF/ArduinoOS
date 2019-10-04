requireend ../../sys/sys.s

; r0=strrchr(r0=haystack addr, r1=needle char), r0 is addr of last occurence, or 0 on failure
label strrchr
; Simply use syscall for speed
mov r2 r1
mov r1 r0
mov r0 SyscallIdStrRChr
syscall
ret
