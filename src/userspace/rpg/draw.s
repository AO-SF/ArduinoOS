requireend ../lib/std/io/fput.s
requireend ../lib/curses/curses.s

requireend level.s
requireend tiles.s

label redraw
; TODO: clear screen, move to start of each row with curses, call drawCellRaw on each cell in row, move onto next row
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
