require lib/sys/sys.s

requireend lib/std/int32/int32fput.s
requireend lib/std/int32/int32shift.s
requireend lib/std/int32/int32str.s
requireend lib/std/int32/int32time.s
requireend lib/std/io/fclose.s
requireend lib/std/io/fopen.s
requireend lib/std/io/fput.s
requireend lib/std/io/fread.s
requireend lib/std/math/mod.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s

ab scratchBuf ArgLenMax
ab pathBuf PathMax
ab dateBuf int32DateSize

aw sampleNum 2 ; 32 bit int

ab fd 1

db usageStr 'usage: datafile samplenum\n',0
db couldNotOpenStr 'Could not open data file\n',0
db couldNotReadStr 'Could not read sample\n',0

; Set fd to invalid
mov r0 fd
mov r1 FdInvalid
store8 r0 r1

; Grab data file path argument
mov r0 SyscallIdArgvN
mov r1 1
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

mov r1 r0
mov r0 pathBuf
call getabspath

; Grab sample num argument
mov r0 SyscallIdArgvN
mov r1 2
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

mov r1 r0
mov r0 sampleNum
call int32fromStr

; Open data file
mov r0 pathBuf
mov r1 FdModeRO
call fopen

mov r1 FdInvalid
cmp r1 r0 r1
skipneq r1
jmp couldNotOpen

mov r1 fd
store8 r1 r0

; Read sample entry into buffer
mov r0 sampleNum
mov r1 3
call int32ShiftLeft

mov r0 fd
load8 r0 r0
mov r1 sampleNum
mov r2 scratchBuf
mov r3 8
call fread32

mov r1 8
cmp r0 r0 r1
skipeq r0
jmp couldNotRead

; Print sample timestamp
mov r0 dateBuf
mov r1 scratchBuf
call int32timeToDate
mov r0 dateBuf
call int32datePrint

mov r0 ' '
call putc0

; Print sample temperature
mov r0 scratchBuf
inc4 r0
load16 r0 r0

push16 r0
mov r1 10
div r0 r0 r1
call putdec
mov r0 '.'
call putc0
pop16 r0
mov r1 10
call mod
call putdec
mov r0 'C'
call putc0

mov r0 ' '
call putc0

; Print sample humidity
mov r0 scratchBuf
inc6 r0
load16 r0 r0

push16 r0
mov r1 10
div r0 r0 r1
call putdec
mov r0 '.'
call putc0
pop16 r0
mov r1 10
call mod
call putdec
mov r0 '%'
call putc0

mov r0 '\n'
call putc0

; Exit (data file closed automatically by OS)
label done
mov r0 0
call exit

; Usage
label usage
mov r0 usageStr
call puts0
jmp done

; Errors
label couldNotOpen
mov r0 couldNotOpenStr
call puts0
jmp done

label couldNotRead
mov r0 couldNotReadStr
call puts0
jmp done
