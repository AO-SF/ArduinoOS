require ../sys/sys.s
requireend ../std/io/fput.s
requireend ../std/io/fputdec.s

const cursesKeyError 65535
const cursesKeyUpArrow 321
const cursesKeyDownArrow 322
const cursesKeyRightArrow 323
const cursesKeyLeftArrow 324

db cursesEscSeqStrClearScreen 27, '[2J', 0
db cursesEscSeqStrClearLine 27, '[2K', 0
db cursesEscSeqStrSetRgb 27, '[38;2;', 0
db cursesEscSeqStrResetAttributes 27, '[0m', 0
db cursesEscSeqStrCursorShow 27, '[?25h', 0
db cursesEscSeqStrCursorHide 27, '[?25l', 0
db cursesEscSeqStrScrollUp 27, '[1T', 0
db cursesEscSeqStrScrollDown 27, '[1S', 0

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
mov r0 cursesEscSeqStrClearScreen
call puts0
ret

; cursesClearLine - clears current line
label cursesClearLine
mov r0 cursesEscSeqStrClearLine
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
mov r0 SyscallIdIoctl
mov r1 FdStdout
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

; cursesGetChar() - puts 16 bit value into r0, see cursesKeyXXX constants
label cursesGetChar
; Try to read a byte from stdin
mov r0 SyscallIdTryReadByte
mov r1 FdStdin
syscall
mov r2 256 ; r2 should be kept equal to 256 for the rest of the function
cmp r1 r0 r2
skipneq r1
jmp cursesGetCharError
; Check for escape sequence
mov r1 27 ; ESC character
cmp r1 r0 r1
skipneq r1
jmp cursesGetCharSequence
; Otherwise return raw byte as read (which is still in r0)
ret
; Handle escape sequences
label cursesGetCharSequence
; Try to read a 2nd byte
mov r0 SyscallIdTryReadByte
mov r1 FdStdin
syscall ; no need to check for error as we explicitly check for '[' char next
; Check for '[' character
mov r1 91 ;
cmp r1 r0 r1
skipeq r1
jmp cursesGetCharError
; Try to read a 3rd byte
mov r0 SyscallIdTryReadByte
mov r1 FdStdin
syscall
cmp r1 r0 r2
skipneq r1
jmp cursesGetCharError
; Return as 16 bit value to differentiate from standard characters
or r0 r0 r2
ret
; Error case
label cursesGetCharError
mov r0 cursesKeyError
ret

; cursesScrollUp() - scroll text up by one line
label cursesScrollUp
mov r0 cursesEscSeqStrScrollUp
call puts0
ret

; cursesScrollDown() - scroll text down by one line
label cursesScrollDown
mov r0 cursesEscSeqStrScrollDown
call puts0
ret
