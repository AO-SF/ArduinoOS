require lib/sys/sys.s

requireend lib/curses/curses.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s

db usageStr 'Usage: less FILE\n',0
db couldNotOpenFileStr 'Could not open file\n',0

ab fileFd 1
ab filePath PathMax

; Interrupt handlers (must be in first 256 bytes)
jmp start

label suicideHandler
jmp quit

label start

; Set fd to invalid before registering suicide handler so we do not close a random fd
mov r0 fileFd
mov r1 0
store8 r0 r1

; Register interrupt handlers
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandler
syscall

; Grab file name from argument
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp usage

; Ensure path is absolute
mov r1 r0
mov r0 filePath
call getabspath

; Attempt to open file in read-only mode
mov r0 SyscallIdOpen
mov r1 filePath
mov r2 FdModeRO
syscall

cmp r1 r0 r0
skipneqz r1
jmp couldNotOpenFile

mov r1 fileFd
store8 r1 r0

; Setup display
mov r0 0
call cursesSetEcho
call cursesClearScreen

; Input loop
label inputLoop
; set cursor to bottom left and print prompt
mov r0 0
mov r1 0 ; TODO: Think about this
call cursesSetPosXY
mov r0 ':'
call putc0
; wait for key press
call cursesGetChar
mov r1 256
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; parse key
mov r1 'q'
cmp r1 r0 r1
skipneq r1
jmp quit

jmp inputLoop

; TODO: rest of program

; Quit
label quit
; close file
mov r0 SyscallIdClose
mov r1 fileFd
load8 r1 r1
cmp r2 r1 r1
skipeqz r2
syscall
; reset display
mov r0 1
call cursesSetEcho
call cursesReset
; exit
mov r0 0
call exit

; Usage
label usage
mov r0 usageStr
call puts0
mov r0 1
call exit

; Could not open file error
label couldNotOpenFile
mov r0 couldNotOpenFileStr
call puts0
mov r0 1
call exit
