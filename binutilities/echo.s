jmp start

ab buf 32

include lib.s

label start
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
mov r0 ' '
call printChar
label skipspace

; Get arg
mov r0 3 ; getargvn
mov r1 r4
mov r2 buf
mov r3 32
syscall

; Write out arg
mov r3 r0
mov r0 257 ; write
mov r1 1 ; stdout
mov r2 buf
syscall

; Inc index
inc r4

; Move onto next argument
jmp loopstart

label loopend

; Print newline
mov r0 '\n'
call printChar

; Exit
mov r0 0
mov r1 0
syscall
