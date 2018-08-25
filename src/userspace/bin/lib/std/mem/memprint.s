require ../io/fputdec.s

; memprint(r0=size) - prints size in bytes, formatted to a human readable form
label memprint

; We only need to worry about kb or bytes
mov r1 1024
cmp r1 r0 r1
skipge r1
jmp memprintbytes

; Print first (ones) digit of kb by dividing size by 1024
push16 r0
mov r1 1024
div r0 r0 r1
call putdec
mov r0 '.'
call putc0
pop16 r0

; Now compute tenths-digit
mov r2 1024
div r1 r0 r2
mul r1 r1 r2
sub r0 r0 r1 ; r0 now contains size-floor(size/1024)*size<1024

mov r1 10
mul r0 r0 r1
div r0 r0 r2
call putdec

mov r0 'k'
call putc0
mov r0 'b'
call putc0

ret

; Print bytes
label memprintbytes
call putdec
mov r0 'b'
call putc0

ret
