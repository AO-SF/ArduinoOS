requireend ../lib/std/io/fget.s
requireend ../lib/std/proc/openpath.s

requireend level.s

label loadLevel ; takes path in r0, returns boolean success value in r0
; attempt to open given path
call openpath
mov r1 r0
cmp r5 r1 r1
skipneqz r5
jmp loadLevelError
; read level
mov r0 256
mov r2 0
mov r3 levelArray
mov r4 levelWH
syscall
; check read return for later
mov r4 levelWH
cmp r4 r0 r4
; close file
mov r0 256
syscall
; return success/failure
mov r0 0
skipneq r4
mov r0 1
ret
; return failure
label loadLevelError
mov r0 0
ret
