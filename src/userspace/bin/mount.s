require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s
requireend lib/std/str/strcmp.s

db typeStrCustomMiniFs 'customminifs',0
db typeStrFlatFile 'flatfile',0
db typeStrPartition1 'partition1',0
db typeStrPartition2 'partition2',0
db typeStrPartition3 'partition3',0
db typeStrPartition4 'partition4',0
db typeStrCircBuf 'circbuf',0

db badTypeErrorStr 'bad type argument\n',0
db usageErrorStr 'usage: mount type device dir\n',0

aw typeId 1
ab devicePath PathMax
ab dirPath PathMax

; Grab type argument
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usageerror;

push16 r0
mov r1 typeStrCustomMiniFs
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypeCustomMiniFs

push16 r0
mov r1 typeStrFlatFile
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypeFlatFile

push16 r0
mov r1 typeStrPartition1
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypePartition1

push16 r0
mov r1 typeStrPartition2
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypePartition2

push16 r0
mov r1 typeStrPartition3
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypePartition3

push16 r0
mov r1 typeStrPartition4
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypePartition4

push16 r0
mov r1 typeStrCircBuf
call strcmp
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp foundTypeCircBuf

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

label foundTypePartition1
mov r0 typeId
mov r1 SyscallMountFormatPartition1
store16 r0 r1
jmp gottypearg

label foundTypePartition2
mov r0 typeId
mov r1 SyscallMountFormatPartition2
store16 r0 r1
jmp gottypearg

label foundTypePartition3
mov r0 typeId
mov r1 SyscallMountFormatPartition3
store16 r0 r1
jmp gottypearg

label foundTypePartition4
mov r0 typeId
mov r1 SyscallMountFormatPartition4
store16 r0 r1
jmp gottypearg

label foundTypeCircBuf
mov r0 typeId
mov r1 SyscallMountFormatCircBuf
store16 r0 r1
jmp gottypearg

label gottypearg

; Read other two arguments
mov r0 SyscallIdArgvN
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp usageerror

mov r1 r0
mov r0 devicePath
call getpath

mov r0 SyscallIdArgvN
mov r1 3
syscall
cmp r1 r0 r0
skipneqz r1
jmp usageerror

mov r1 r0
mov r0 dirPath
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
