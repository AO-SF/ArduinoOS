require int32common.s

requireend int32cmp.s
requireend int32div.s
requireend int32mul.s
requireend int32fput.s

dw int32MemPrintBytesPerKb 0,1024
dw int32MemPrintBytesPerMb 16,0
dw int32MemPrintBytesPerGb 16384,0

db int32MemPrintBytesStr 'b',0
db int32MemPrintKbStr 'kb',0
db int32MemPrintMbStr 'mb',0
db int32MemPrintGbStr 'gb',0

aw int32MemPrintRemainder 2

; int32MemPrint(x=r0) - prints x as human readable memory size e.g. x=2048 produces '2.0kb'
; Note: this modifies the value passed to it
label int32MemPrint ; takes pointer to 32 bit value in r0, giving number of bytes
; Check for >=1gb
push16 r0
mov r1 int32MemPrintBytesPerGb
call int32LessThan
mov r1 r0
pop16 r0
cmp r1 r1 r1
skipneqz r1
jmp int32MemPrintGb
; Check for >=1mb
push16 r0
mov r1 int32MemPrintBytesPerMb
call int32LessThan
mov r1 r0
pop16 r0
cmp r1 r1 r1
skipneqz r1
jmp int32MemPrintMb
; Check for >=1kb
push16 r0
mov r1 int32MemPrintBytesPerKb
call int32LessThan
mov r1 r0
pop16 r0
cmp r1 r1 r1
skipneqz r1
jmp int32MemPrintKb
; Otherwise simply <1024 bytes
call int32put0
mov r0 int32MemPrintBytesStr
call puts0
ret
; Gb case
label int32MemPrintGb
push16 r0
mov r1 int32MemPrintBytesPerGb
mov r2 int32MemPrintRemainder
call int32div32rem
pop16 r0
call int32put0
mov r0 '.'
call putc0
mov r0 int32MemPrintRemainder
mov r1 Int32Const1E1
call int32mul32
mov r0 int32MemPrintRemainder
mov r1 int32MemPrintBytesPerGb
call int32div32
mov r0 int32MemPrintRemainder
call int32put0
mov r0 int32MemPrintGbStr
call puts0
ret
; Mb case
label int32MemPrintMb
push16 r0
mov r1 int32MemPrintBytesPerMb
mov r2 int32MemPrintRemainder
call int32div32rem
pop16 r0
call int32put0
mov r0 '.'
call putc0
mov r0 int32MemPrintRemainder
mov r1 Int32Const1E1
call int32mul32
mov r0 int32MemPrintRemainder
mov r1 int32MemPrintBytesPerMb
call int32div32
mov r0 int32MemPrintRemainder
call int32put0
mov r0 int32MemPrintMbStr
call puts0
ret
; Kb case
label int32MemPrintKb
push16 r0
mov r1 int32MemPrintBytesPerKb
mov r2 int32MemPrintRemainder
call int32div32rem
pop16 r0
call int32put0
mov r0 '.'
call putc0
mov r0 int32MemPrintRemainder
mov r1 Int32Const1E1
call int32mul32
mov r0 int32MemPrintRemainder
mov r1 int32MemPrintBytesPerKb
call int32div32
mov r0 int32MemPrintRemainder
call int32put0
mov r0 int32MemPrintKbStr
call puts0
ret
