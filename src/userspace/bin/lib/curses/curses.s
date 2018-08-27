requireend ../std/io/fput.s
requireend ../std/io/fputdec.s

db cursesEscSeqStrClear 27, '[2J', 0
db cursesEscSeqStrSetRgb 27, '[38;2;', 0
db cursesEscSeqStrResetAttributes 27, '[0m', 0
db cursesEscSeqStrCursorShow 27, '[?25h', 0
db cursesEscSeqStrCursorHide 27, '[?25l', 0

; cursesReset - clear screen and set (visible) cursor to (0,0)
label cursesReset
call cursesResetAttributes
mov r0 1
call cursesCursorSetVisible
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

; cursesCursorSetVisible (r0=boolean visible value)
label cursesCursorSetVisible
cmp r0 r0 r0
skipneqz r0
jmp cursesCursorHide
jmp cursesCursorShow

label cursesCursorShow
mov r0 cursesEscSeqStrCursorShow
call puts0
ret

label cursesCursorHide
mov r0 cursesEscSeqStrCursorHide
call puts0
ret

; cursesResetAttributes
label cursesResetAttributes
mov r0 cursesEscSeqStrResetAttributes
call puts0
ret

; cursesSetEcho(value=r0) - turns terminal echo on/off
label cursesSetEcho
mov r3 r0 ; on/off value
mov r0 SyscallIdEnvGetStdoutFd
syscall
mov r1 r0 ; fd for stdout
mov r0 1283 ; ioctl syscall id
mov r2 0 ; set echo command
syscall
ret

; cursesSetPosXY(x=r0, y=r1)
label cursesSetPosXY
inc r0
inc r1
push8 r0
push8 r1
mov r0 27
call putc0
mov r0 '['
call putc0
pop8 r0 ; y
call putdec
mov r0 ';'
call putc0
pop8 r0 ; x
call putdec
mov r0 'H'
call putc0
ret

; cursesSetColour(r=r0, g=r1, b=r2) - where r,g,b are in the range [0, 255]
label cursesSetColour
push8 r2
push8 r1
push8 r0
mov r0 cursesEscSeqStrSetRgb
call puts0
pop8 r0 ; r
call putdec
mov r0 ';'
call putc0
pop8 r0 ; g
call putdec
mov r0 ';'
call putc0
pop8 r0 ; b
call putdec
mov r0 'm'
call putc0
ret

; cursesGetChar() - puts single byte into r0, or 256 if no data to read
label cursesGetChar
; grab stdio fd
mov r0 SyscallIdEnvGetStdinFd
syscall
; call tryreadbyte syscall
mov r1 r0
mov r0 264
syscall
ret
