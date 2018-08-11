requireend ../lib/std/io/fget.s
requireend ../lib/std/proc/openpath.s

requireend level.s

ab loadScratchByte 1

label loadLevel ; takes path in r0, returns boolean success value in r0
; attempt to open given path
call openpath
mov r1 r0
cmp r5 r1 r1
skipneqz r5
jmp loadLevelError
; load loop init (fd is in r1)
mov r2 0 ; loop index/file offset
label loadLevelLoopStart
; loaded full level size?
mov r5 levelWH
cmp r5 r2 r5
skipneq r5
jmp loadLevelLoopEnd
; attempt to read next byte
mov r0 256
mov r3 loadScratchByte
mov r4 1
syscall
cmp r0 r0 r0
skipneqz r0
jmp loadLevelError
; store byte into level array
mov r0 levelArray
add r0 r0 r2
mov r5 loadScratchByte
load8 r5 r5
store8 r0 r5
; loop around to read next byte
inc r2
jmp loadLevelLoopStart
label loadLevelLoopEnd
; close file
mov r1 r0
mov r0 256
syscall
; return success
mov r0 1
ret
; return failure
label loadLevelError
mov r0 0
ret
