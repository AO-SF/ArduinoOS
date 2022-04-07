require ../../sys/syscall.s

requireend ../io/fput.s
requireend ../io/fputdec.s

requireend int32common.s
requireend int32cmp.s
requireend int32div.s
requireend int32get.s
requireend int32set.s

const int32DateSize 7 ; 2 bytes for year, 1 each for others

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

; int32timeToDate(r0=dateDestPtr, r1=srcTimePtr) - decompose 32 bit time in seconds since epoch into a date struct with y/m/d/h/m/s.
label int32timeToDate
mov r2 r1
mov r1 r0
mov r0 SyscallIdTimeToDate32s
syscall
ret

; int32dateGetYear(r0=datePtr) - returns year in r0 for given date
label int32dateGetYear
load16 r0 r0
ret

; int32dateGetMonth(r0=datePtr) - returns month in r0 for given date (1-12)
label int32dateGetMonth
inc2 r0
load8 r0 r0
ret

; int32dateGetDay(r0=datePtr) - returns day in r0 for given date (1-31)
label int32dateGetDay
inc3 r0
load8 r0 r0
ret

; int32dateGetHour(r0=datePtr) - returns hour in r0 for given date (0-23)
label int32dateGetHour
inc4 r0
load8 r0 r0
ret

; int32dateGetMinute(r0=datePtr) - returns minute in r0 for given date (0-59)
label int32dateGetMinute
inc5 r0
load8 r0 r0
ret

; int32dateGetSecond(r0=datePtr) - returns second in r0 for given date (0-59)
label int32dateGetSecond
inc6 r0
load8 r0 r0
ret

; int32datePrint(r0=datePtr)
label int32datePrint
; Print y/m/d
push16 r0
call int32dateGetDay
mov r1 2
call putdecpad
mov r0 '/'
call putc0
pop16 r0
push16 r0
call int32dateGetMonth
mov r1 2
call putdecpad
mov r0 '/'
call putc0
pop16 r0
push16 r0
call int32dateGetYear
mov r1 2
call putdecpad
; Print space
mov r0 ' '
call putc0
; Print h:m:s
pop16 r0
push16 r0
call int32dateGetHour
mov r1 2
call putdecpad
mov r0 ':'
call putc0
pop16 r0
push16 r0
call int32dateGetMinute
mov r1 2
call putdecpad
mov r0 ':'
call putc0
pop16 r0
call int32dateGetSecond
mov r1 2
call putdecpad
ret
