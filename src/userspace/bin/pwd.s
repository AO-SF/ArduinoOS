require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/getpwd.s
requireend lib/std/proc/exit.s

ab pwdBuf PathMax

; Grab pwd
mov r0 pwdBuf
call getpwd

; Print pwd and newline
mov r0 pwdBuf
call puts0

mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
