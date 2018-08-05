requireend ../std/io/fput.s
requireend ../std/io/fputdec.s

db cursesEscSeqStrClear 27, '[2J', 0

label cursesClearScreen
mov r0 cursesEscSeqStrClear
call puts0
ret

; cursesSetPosXY(x=r0, y=r1)
label cursesSetPosXY
push r0
push r1

mov r0 27
call putc0
mov r0 '['
call putc0
pop r0 ; y
call putdec
mov r0 ';'
call putc0
pop r0 ; x
call putdec
mov r0 'H'
call putc0

ret
