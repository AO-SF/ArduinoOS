require ../../sys/syscall.s

; getpwd() - returns pointer to pwd string in r0
label getpwd
mov r0 SyscallIdEnvGetPwd
syscall
ret
