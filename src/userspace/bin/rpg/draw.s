requireend ../lib/std/io/fput.s
requireend ../lib/curses/curses.s

requireend level.s
requireend tiles.s

label redraw
; clear screen and return to top left
call cursesReset
; row loop init
mov r1 0 ; current row
label redrawRowLoopStart
; drawn all rows?
mov r4 levelH
cmp r4 r1 r4
skiplt r4
jmp redrawRowLoopEnd
; draw first cell of this row (this moves the cursor for us)
mov r0 0 ; column=0
push r1
call updateCell
pop r1
; loop over all other cells in this row
mov r2 1 ; current column
label redrawColumnLoopStart
; Reached end of row?
mov r4 levelW
cmp r4 r2 r4
skiplt r4
jmp redrawColumnLoopEnd
; Draw this cell
push r1
push r2
mov r0 r2
call drawCellRaw
pop r2
pop r1
; Advance to next column
inc r2
jmp redrawColumnLoopStart
label redrawColumnLoopEnd
; Advance to next row
inc r1
jmp redrawRowLoopStart
label redrawRowLoopEnd
call cursesResetAttributes
ret

label drawCellRaw ; takes x and y in r0 and r1
call levelLoadCell
call tilesGetPrintString
call puts0
ret

label updateCell ; takes x and y in r0 and r1
push r0
push r1
call cursesSetPosXY
pop r1
pop r0
call drawCellRaw
ret
