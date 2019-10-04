require lib/sys/sys.s

requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s
requireend lib/std/rand/rand.s
requireend lib/std/str/strtoint.s

aw max 1 ; upper limit set by player
aw value 1 ; random value in [1,max]
ab attempts 1 ; number of attempts used

const scratchBufSize 8
ab scratchBuf scratchBufSize

db maxPromptStr 'Enter maximum value: ',0

db badMaxStr 'Bad maximum value - please enter an integer greater than one.\n',0

db startPreStr 'I have a chosen a random integer between 1 and ',0
db startPostStr '.\n',0

db guessPromptPreStr 'Attempt ',0
db guessPromptPostStr ', enter your guess: ',0

db guessCorrectStr 'You guessed correctly!\n',0
db guessLowStr 'Your guess was too low.\n',0
db guessHighStr 'Your guess was too high.\n',0

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Set attempts to 0
mov r0 attempts
mov r1 0
store8 r0 r1

; Ask player for maximum
mov r0 maxPromptStr
call puts0

mov r0 scratchBuf
mov r1 scratchBufSize
call gets0

mov r0 scratchBuf
call strtoint

mov r1 max
store16 r1 r0

; Bad maximum?
mov r1 2
cmp r1 r0 r1
skipge r1
jmp badMax

; Print start string to explain
mov r0 startPreStr
call puts0

mov r0 max
load16 r0 r0
call putdec

mov r0 startPostStr
call puts0

; Pick random value
mov r0 max
load16 r0 r0
call randN
inc r0
mov r1 value
store16 r1 r0

; Start of guessing loop
label gameLoopStart

; Increase attempts
mov r0 attempts
load8 r1 r0
inc r1
store8 r0 r1

; Print prompt string
mov r0 guessPromptPreStr
call puts0
mov r0 attempts
load8 r0 r0
call putdec
mov r0 guessPromptPostStr
call puts0

; Grab guess
mov r0 scratchBuf
mov r1 scratchBufSize
call gets0

mov r0 scratchBuf
call strtoint

; Check for high/low/correct
mov r1 value
load16 r1 r1
cmp r2 r0 r1
skipneq r2
jmp guessCorrect
skipgt r2
jmp guessLow
jmp guessHigh

; Player guessed correctly
label guessCorrect
; Print message
mov r0 guessCorrectStr
call puts0
; Quit
jmp done

; Player guessed low
label guessLow
; Print message
mov r0 guessLowStr
call puts0
; Loop again to let user try again
jmp gameLoopStart

; Player guessed high
label guessHigh
; Print message
mov r0 guessHighStr
call puts0
; Loop again to let user try again
jmp gameLoopStart

; Exit
label done
mov r0 0
call exit

label badMax
mov r0 badMaxStr
call puts0
jmp done
