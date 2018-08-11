requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s

ab buf 64

; Get argc
mov r0 2 ; getargc
syscall
push r0

; Init ready for loop
mov r4 1

label loopstart

; Check if hit argc
pop r0
push r0
cmp r0 r0 r4
skipneq r0
jmp loopend

; Do we need to print a space?
mov r0 1
cmp r0 r4 r0
skipgt r0
jmp skipspace

; Print space
push r0
push r4
mov r0 ' '
call putc0
pop r4
pop r0
label skipspace

; Get arg
mov r0 3 ; getargvn
mov r1 r4
mov r2 buf
mov r3 64
syscall

; Write out arg
push r0
push r4
mov r0 buf
call puts0
pop r4
pop r0

; Move onto next argument
inc r4
jmp loopstart

label loopend

; Print newline
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
