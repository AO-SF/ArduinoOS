require ../../sys/syscall.s

; runpath(r0=argc, r1=argv) - executes a program as the shell would - checking in PATH first if path has no slashes, otherwise making absolute if needed by preprending pwd. on sucess does not return
label runpath
mov r3 SyscallExecPathFlagSearch
mov r2 r1
mov r1 r0
mov r0 SyscallIdExec
syscall
ret
