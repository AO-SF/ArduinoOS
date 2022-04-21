require ../../sys/syscall.s

; r0=strreplace(haystack=r0, needle=r1, replace=r2) - note: for now this assumes strlen(replace)<=strlen(needle)
label strreplace
; Use syscall for speed
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdStrReplace
syscall
ret
