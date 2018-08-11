requireend ../lib/std/io/fput.s
requireend ../lib/curses/curses.s

requireend level.s
requireend tiles.s

label redraw
; TODO: clear screen, move to start of each row with curses, call drawCellRaw on each cell in row, move onto next row
ret

label drawCellRaw
; TODO: load tile index from cell in level array and simply print tile colour+char string
ret

label updateCell
; TODO: move cursor to correct position, then call drawCellRaw
ret
