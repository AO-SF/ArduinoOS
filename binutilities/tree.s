jmp start

require lib/std/io/fput.s
require lib/std/proc/exit.s
require lib/std/str/strcpy.s
require lib/std/str/strlen.s

db rootPath '/', 0
ab pathBuf 64

label start

; Copy root path into path buffer
mov r0 pathBuf
mov r1 rootPath
call strcpy

; Call printDir
mov r0 0
call printDir

; Exit
mov r0 0
call exit

; Recursive printDir function (uses path in pathBuf initially, but clobbers it before returning)
label printDir

; Indent as required
push r0
mov r1 r0 ; move indent into r1 as a loop variable
label printDirIndentLoopStart
cmp r0 r1 r1 ; finished?
skipneqz r0
jmp printDirIndentLoopEnd
push r1 ; print tab
mov r0 ' '
call putc0
pop r1
dec r1 ; decrement remaining count and loop again
jmp printDirIndentLoopStart
label printDirIndentLoopEnd
pop r0

; Print name and a newline
mov r1 1
cmp r1 r0 r1
skiple r1
inc r0

mov r1 pathBuf
add r0 r0 r1
call puts0
mov r0 '\n'
call putc0

; Store path length to strip from front of children if needed
mov r0 pathBuf
call strlen
mov r4 r0 ; r4 contains length

; Attempt to open file (so if it is a dir we can read the children)
mov r0 258
mov r1 pathBuf
syscall

; Bad fd?
cmp r1 r0 r0
skipneqz r1
jmp printDirDone

; Child loop init
mov r1 r0 ; fd stored in r1
mov r2 0 ; child num
; r4 still contains parent path length

label printDirChildLoopStart
; Call getchildn
mov r0 260
mov r3 pathBuf
syscall

; No child?
cmp r0 r0 r0
skipneqz r0
jmp printDirChildLoopEnd

; Call printDir recursively for child
push r1
push r2
push r4
mov r0 r4
call printDir
pop r4
pop r2
pop r1

; Loop again to try for next child
inc r2
jmp printDirChildLoopStart
label printDirChildLoopEnd

; Close file
mov r0 259
syscall

; Done
label printDirDone
ret
