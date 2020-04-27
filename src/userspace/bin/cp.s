require lib/sys/sys.s

requireend lib/std/int32/int32add.s
requireend lib/std/int32/int32set.s
requireend lib/std/io/fget.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/proc/getpath.s
requireend lib/std/proc/openpath.s

db usageStr 'usage: cp SOURCE DEST\n', 0
db errorStrBadSource 'could not open source\n', 0
db errorStrBadDest 'could not create/open dest\n', 0

ab sourceArg PathMax
ab destArg PathMax
ab sourceFd 1
ab destFd 1

ab copyBuf PathMax

aw fileOffset 2 ; 32 bit integer

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Get source argument and call getpath on it
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp showUsage

mov r1 r0
mov r0 sourceArg
call getpath

; Get dest argument and call getpath on it
mov r0 SyscallIdArgvN
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp showUsage

mov r1 r0
mov r0 destArg
call getpath

; Open source file
mov r0 sourceArg
mov r1 FdModeRO
call openpath
cmp r1 r0 r0
skipneqz r1
jmp couldNotOpenSource
mov r1 sourceFd
store8 r1 r0

; Create/resize dest path
mov r0 SyscallIdGetFileLen32
mov r1 sourceArg
mov r2 fileOffset
syscall
mov r2 r0
mov r0 SyscallIdResizeFile32
mov r1 destArg
mov r2 fileOffset
syscall

; Open dest
mov r0 destArg
mov r1 FdModeWO
call openpath
cmp r1 r0 r0
skipneqz r1
jmp couldNotOpenDest
mov r1 destFd
store8 r1 r0

; Copy bytes from source file to dest file
mov r0 fileOffset
mov r1 0
call int32set16
label cpCopyLoopStart
; Read a byte from source
mov r0 SyscallIdRead32
mov r1 sourceFd
load8 r1 r1
mov r2 fileOffset
mov r3 copyBuf
mov r4 1
syscall
cmp r0 r0 r0
skipneqz r0
jmp cpCopyLoopEnd
; Write byte
mov r0 SyscallIdWrite32
mov r1 destFd
load8 r1 r1
mov r2 fileOffset
mov r3 copyBuf
mov r4 1
syscall
cmp r0 r0 r0
skipneqz r0
jmp cpCopyLoopEnd
; Loop again to copy next byte
mov r0 fileOffset
call int32inc
jmp cpCopyLoopStart
label cpCopyLoopEnd

; Exit (note: no need to close files as they are reclaimed by the OS automatically)
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
