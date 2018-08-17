requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s
requireend lib/std/str/strequal.s

const typeIdCustomMiniFs 0
const typeIdFlatFile 1

db typeStrCustomMiniFs 'customminifs',0
db typeStrFlatFile 'flatfile',0

db badTypeErrorStr 'bad type argument\n',0
db usageErrorStr 'usage: mount type device dir\n',0

ab scratchBuf 64

ab typeArg 64
aw typeId 1
ab devicePath 64
ab dirPath 64

; Grab type argument
mov r0 3
mov r1 1
mov r2 typeArg
mov r3 64
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

mov r0 badTypeErrorStr
call puts0
mov r0 1
call exit

label foundTypeCustomMiniFs
mov r0 typeId
mov r1 typeIdCustomMiniFs
store16 r0 r1
jmp gottypearg

label foundTypeFlatFile
mov r0 typeId
mov r1 typeIdFlatFile
store16 r0 r1
jmp gottypearg

label gottypearg

; Read other two arguments
mov r0 3
mov r1 2
mov r2 scratchBuf
mov r3 64
syscall
cmp r0 r0 r0
skipneqz r0
jmp usageerror
mov r0 devicePath
mov r1 scratchBuf
call getpath

mov r0 3
mov r1 3
mov r2 scratchBuf
mov r3 64
syscall
cmp r0 r0 r0
skipneqz r0
jmp usageerror
mov r0 dirPath
mov r1 scratchBuf
call getpath

; Invoke mount syscall
mov r0 1281
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
