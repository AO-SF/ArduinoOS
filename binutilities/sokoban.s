requireend lib/curses/curses.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/openpath.s

db errorStrNoArg 'usage: sokoban LEVELPATH\n', 0
db errorStrBadLevel 'error: could not load given level\n', 0

db maxSize 16
ab levelArray 256 ; maxSize*maxSize

ab pathBuf 64
ab scratchByte 1

; Grab level path from arg
mov r0 3
mov r1 1
mov r2 pathBuf
mov r3 64
syscall

cmp r0 r0 r0
skipneqz r0
jmp errorNoArg

; Read in level
mov r0 pathBuf
call levelRead

cmp r0 r0 r0
skipneqz r0
jmp errorBadLevel

; Draw level
call levelDraw

; TODO: Handle WASD and physics logic, using curses to update screen as needed

; Exit
label done
mov r0 0
call exit

; Errors
label errorNoArg
mov r0 errorStrNoArg
call puts0
mov r0 1
call exit

label errorBadLevel
mov r0 errorStrBadLevel
call puts0
mov r0 1
call exit

; Read a level - takes path in r0, returns true/false in r0
label levelRead
; Open level file
call openpath
cmp r1 r0 r0
skipneqz r1
jmp levelReadFail

; load level into array (fd is in r0)
mov r2 0 ; read offset
mov r4 0 ; row (y)
mov r5 0 ; column (x)
label levelReadLoopStart
; read a byte
push r0
push r4
mov r1 r0
mov r0 256
mov r3 scratchByte
mov r4 1
syscall
inc r2
mov r1 r0
pop r4
pop r0

; EOF?
cmp r1 r1 r1
skipneqz r1
jmp levelReadLoopEnd

; store character in current row
mov r1 levelArray
add r1 r1 r5
mov r3 maxSize
load8 r3 r3
mul r3 r3 r4
add r1 r1 r3
mov r3 scratchByte
load8 r3 r3
store8 r1 r3

; newline?
mov r1 '\n'
cmp r1 r3 r1
skipeq r1
jmp levelReadLoopNotNewline
inc r4 ; advance current row
mov r5 0 ; reset x
jmp levelReadLoopStart
label levelReadLoopNotNewline

; advance to next cell in x direction and loop
inc r5
jmp levelReadLoopStart
label levelReadLoopEnd

; Close file
mov r1 r0
mov r0 259

; Success
mov r0 1
ret

label levelReadFail
mov r0 0
ret

; Draw a level
label levelDraw

; clear screen
call cursesReset

; loop over all cells
mov r4 0 ; y
label levelDrawYStart
mov r3 0 ; x
label levelDrawXStart

; load at data at (x,y)
push r3
push r4
mov r0 r3
mov r1 r4
call levelLoadCell
pop r4
pop r3

; print this character
push r0
push r3
push r4
call putc0
pop r4
pop r3
pop r0

; end of row?
mov r1 '\n'
cmp r1 r0 r1
skipeq r1
jmp levelDrawXContinue
; end of level? (0 length row)
cmp r1 r3 r3
skipneqz r1
jmp levelDrawYEnd ; end of level
; end of row - inc y
inc r4
jmp levelDrawYStart

; advance to next cell
label levelDrawXContinue
inc r3
jmp levelDrawXStart

; end of a line
inc r4
jmp levelDrawYStart
label levelDrawYEnd

ret

; levelLoadCell(x=r0, y=r1) - returns cell value in r0
label levelLoadCell
mov r2 levelArray ; base offset
add r0 r0 r2 ; add x
mov r2 maxSize ; add y*maxSize
load8 r2 r2
mul r2 r2 r1
add r0 r0 r2
load8 r0 r0 ; load cell
ret
