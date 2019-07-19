require ../../sys/syscall.s

; getpwd(r0=dest addr)
label getpwd
mov r1 r0
mov r0 SyscallIdEnvGetPwd
syscall
ret
