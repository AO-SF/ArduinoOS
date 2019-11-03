require ../../sys/syscall.s

requireend ../io/fput.s
requireend ../io/fputdec.s

requireend int32common.s
requireend int32cmp.s
requireend int32div.s
requireend int32get.s
requireend int32set.s

aw int32puttimeScratchInt32A 2
aw int32puttimeScratchInt32B 2

; int32gettimemonotonic(r0=dest) - stores number of seconds since booting into 32 bit value pointed to by dest
label int32gettimemonotonic
mov r1 r0
mov r0 SyscallIdTimeMonotonic32s
syscall
ret

; int32gettimereal(r0=dest) - stores number of seconds since 1st Jan 1970 into 32 bit value pointed to by dest
label int32gettimereal
mov r1 r0
mov r0 SyscallIdTimeReal32s
syscall
ret

; int32puttime(r0=time) - prints time in a human readable format, e.g. 5m36s
label int32puttime
; Copy given time to scratch variable that we can modify
mov r1 r0
mov r0 int32puttimeScratchInt32A
call int32set32

; Check for days
mov r0 int32puttimeScratchInt32A
mov r1 Int32Const86400
call int32LessThan
cmp r0 r0 r0
skipeqz r0
jmp int32puttimeNoDays
; Yes we have days, divide to find how many
mov r0 int32puttimeScratchInt32B
mov r1 int32puttimeScratchInt32A
call int32set32
mov r0 int32puttimeScratchInt32B ; result will be stored here
mov r1 Int32Const86400
mov r2 int32puttimeScratchInt32A ; ScratchA gets the remainder which is used in next iteration
call int32div32rem
; Print days
mov r0 int32puttimeScratchInt32B
call int32getLower16
call putdec
mov r0 'd'
call putc0
label int32puttimeNoDays

; Check for hours
mov r0 int32puttimeScratchInt32A
mov r1 Int32Const3600
call int32LessThan
cmp r0 r0 r0
skipeqz r0
jmp int32puttimeNoHours
; Yes we have hours, divide to find how many
mov r0 int32puttimeScratchInt32B
mov r1 int32puttimeScratchInt32A
call int32set32
mov r0 int32puttimeScratchInt32B ; result will be stored here
mov r1 Int32Const3600
mov r2 int32puttimeScratchInt32A ; ScratchA gets the remainder which is used in next iteration
call int32div32rem
; Print hours
mov r0 int32puttimeScratchInt32B
call int32getLower16
call putdec
mov r0 'h'
call putc0
label int32puttimeNoHours

; Check for minutes
mov r0 int32puttimeScratchInt32A
mov r1 Int32Const60
call int32LessThan
cmp r0 r0 r0
skipeqz r0
jmp int32puttimeNoMinutes
; Yes we have minutes, divide to find how many
mov r0 int32puttimeScratchInt32B
mov r1 int32puttimeScratchInt32A
call int32set32
mov r0 int32puttimeScratchInt32B ; result will be stored here
mov r1 Int32Const60
mov r2 int32puttimeScratchInt32A ; ScratchA gets the remainder which is used in next iteration
call int32div32rem
; Print minutes
mov r0 int32puttimeScratchInt32B
call int32getLower16
call putdec
mov r0 'm'
call putc0
label int32puttimeNoMinutes

; Remainder is seconds
mov r0 int32puttimeScratchInt32A
call int32getLower16
call putdec
mov r0 's'
call putc0
ret
