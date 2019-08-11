require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s
requireend lib/std/str/strequal.s

db typeStrCustomMiniFs 'customminifs',0
db typeStrFlatFile 'flatfile',0
db typeStrFat 'fat',0

db badTypeErrorStr 'bad type argument\n',0
db usageErrorStr 'usage: mount type device dir\n',0

ab scratchBuf PathMax

ab typeArg ArgLenMax
aw typeId 1
ab devicePath PathMax
ab dirPath PathMax

; Grab type argument
mov r0 SyscallIdArgvN
mov r1 1
mov r2 typeArg
mov r3 ArgLenMax
syscall

mov r0 typeArg
mov r1 typeStrCustomMiniFs
call strequal
cmp r0 r0 r0
skipeqz r0
jmp foundTypeCustomMiniFs

mov r0 typeArg
mov r1 typeStrFlatFile
call strequal
cmp r0 r0 r0
skipeqz r0
jmp foundTypeFlatFile

mov r0 typeArg
mov r1 typeStrFat
call strequal
cmp r0 r0 r0
skipeqz r0
jmp foundTypeFat

mov r0 badTypeErrorStr
call puts0
mov r0 1
call exit

label foundTypeCustomMiniFs
mov r0 typeId
mov r1 SyscallMountFormatCustomMiniFs
store16 r0 r1
jmp gottypearg

label foundTypeFlatFile
mov r0 typeId
mov r1 SyscallMountFormatFlatFile
store16 r0 r1
jmp gottypearg

label foundTypeFat
mov r0 typeId
mov r1 SyscallMountFormatFat
store16 r0 r1
jmp gottypearg

label gottypearg

; Read other two arguments
mov r0 SyscallIdArgvN
mov r1 2
mov r2 scratchBuf
mov r3 PathMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usageerror
mov r0 devicePath
mov r1 scratchBuf
call getpath

mov r0 SyscallIdArgvN
mov r1 3
mov r2 scratchBuf
mov r3 PathMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp usageerror
mov r0 dirPath
mov r1 scratchBuf
call getpath

; Invoke mount syscall
mov r0 SyscallIdMount
mov r1 typeId
load16 r1 r1
mov r2 devicePath
mov r3 dirPath
syscall

; Exit
mov r0 0
call exit

; Usage error
label usageerror
mov r0 usageErrorStr
call puts0
mov r0 1
call exit
