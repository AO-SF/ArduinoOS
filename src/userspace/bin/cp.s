require lib/sys/sys.s

requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/openpath.s

db usageStr 'usage: cp SOURCE DEST\n', 0
db errorStrBadSource 'could not open source\n', 0
db errorStrBadDest 'could not create/open dest\n', 0

ab sourceArg PathMax
ab destArg PathMax
ab sourceFd 1
ab destFd 1

ab cpScratchBuf PathMax

; Get source and dest arguments
mov r0 3
mov r1 1
mov r2 sourceArg
mov r3 PathMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp showUsage
mov r0 3
mov r1 2
mov r2 destArg
mov r3 PathMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp showUsage

; Call getpath on source path
mov r0 cpScratchBuf
mov r1 sourceArg
call strcpy
mov r0 sourceArg
mov r1 cpScratchBuf
call getpath

; Open source file
mov r0 sourceArg
call openpath
cmp r1 r0 r0
skipneqz r1
jmp couldNotOpenSource
mov r1 sourceFd
store8 r1 r0

; Call getpath on dest path
mov r0 cpScratchBuf
mov r1 destArg
call strcpy
mov r0 destArg
mov r1 cpScratchBuf
call getpath

; Create/resize dest path
mov r0 263
mov r1 sourceArg
syscall
mov r2 r0
mov r0 262
mov r1 destArg
syscall

; Open dest
mov r0 destArg
call openpath
cmp r1 r0 r0
skipneqz r1
jmp couldNotOpenDest
mov r1 destFd
store8 r1 r0

; Copy bytes from source file to dest file
mov r2 0 ; loop index and read/write offset
label cpCopyLoopStart
; Read a byte from source
mov r0 256
mov r1 sourceFd
load8 r1 r1
mov r3 cpScratchBuf
mov r4 1
syscall
cmp r0 r0 r0
skipneqz r0
jmp cpCopyLoopEnd
; Write byte
mov r0 257
mov r1 destFd
load8 r1 r1
mov r3 cpScratchBuf
mov r4 1
syscall
cmp r0 r0 r0
skipneqz r0
jmp cpCopyLoopEnd
; Loop again to copy next byte
inc r2
jmp cpCopyLoopStart
label cpCopyLoopEnd

; Close dest file
mov r0 259
mov r1 destFd
load8 r1 r1
syscall

; Close source file
mov r0 259
mov r1 sourceFd
load8 r1 r1
syscall

; Exit
mov r0 0
call exit
; Errors
label showUsage
mov r0 usageStr
call puts0
mov r0 1
call exit
label couldNotOpenSource
mov r0 errorStrBadSource
call puts0
mov r0 1
call exit
label couldNotOpenDest
mov r0 errorStrBadDest
call puts0
mov r0 1
call exit
