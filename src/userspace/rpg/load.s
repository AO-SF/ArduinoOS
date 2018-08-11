requireend ../lib/std/io/fget.s

requireend level.s

label loadLevel ; takes path in r0, returns boolean success value in r0
; TODO: Read fixed sized level from file (based on constants in level.s), then call redraw
mov r0 0 ; failure for now
ret
