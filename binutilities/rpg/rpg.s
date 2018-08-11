requireend ../lib/std/proc/exit.s

requireend draw.s
requireend level.s
requireend load.s
requireend tiles.s

; Start
; TODO: Remove proof of concept
requireend ../lib/std/io/fput.s
mov r0 tileData
call puts0
mov r0 tileData
mov r1 16
add r0 r0 r1
call puts0
mov r0 tileData
mov r1 32
add r0 r0 r1
call puts0
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
