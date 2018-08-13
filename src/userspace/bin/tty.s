requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s

ab pathBuf 64

; Grab stdio fd
mov r0 512
syscall

; Grab path
mov r1 r0
mov r0 261
mov r2 pathBuf
syscall

; Check return
; cmp r0 r0 r0
; skipeqz r0
; jmp print
;
; mov r0 1
; call exit

; Print path
;label print
mov r0 pathBuf
call puts0
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
