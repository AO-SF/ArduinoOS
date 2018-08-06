requireend ../std/io/fput.s
requireend ../std/io/fputdec.s

db cursesEscSeqStrClear 27, '[2J', 0
db cursesEscSeqStrSetRgb 27, '[38;2;', 0

; cursesReset - clear screen and set cursor to (0,0)
label cursesReset
call cursesClearScreen
mov r0 0
mov r1 0
call cursesSetPosXY
ret

; cursesClearScreen
label cursesClearScreen
mov r0 cursesEscSeqStrClear
call puts0
ret

; cursesSetPosXY(x=r0, y=r1)
label cursesSetPosXY
inc r0
inc r1
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

; cursesSetColour(r=r0, g=r1, b=r2) - where r,g,b are in the range [0, 255]
label cursesSetColour
push r2
push r1
push r0

mov r0 cursesEscSeqStrSetRgb
call puts0

pop r0 ; r
call putdec
mov r0 ';'
call putc0
pop r0 ; g
call putdec
mov r0 ';'
call putc0
pop r0 ; b
call putdec

mov r0 'm'
call putc0

ret

; cursesGetChar() - puts single byte into r0, or 256 if no data to read
label cursesGetChar
; grab stdio fd
mov r0 512
syscall
; call tryreadbyte syscall
mov r1 r0
mov r0 264
syscall
ret
