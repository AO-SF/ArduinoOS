requireend lib/std/int32/int32time.s
requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/proc/exit.s

aw currTime32 2 ; 32 bit time value
ab currDate int32DateSize

; Get real time in seconds since the epoch
mov r0 currTime32
call int32gettimereal

; Convert to date struct
mov r0 currDate
mov r1 currTime32
call int32timeToDate

; Print d/m/y
mov r0 currDate
call int32dateGetDay
mov r1 2
call putdecpad
mov r0 '/'
call putc0
mov r0 currDate
call int32dateGetMonth
mov r1 2
call putdecpad
mov r0 '/'
call putc0
mov r0 currDate
call int32dateGetYear
call putdec

mov r0 ' '
call putc0

; Print h:m:s
mov r0 currDate
call int32dateGetHour
mov r1 2
call putdecpad
mov r0 ':'
call putc0
mov r0 currDate
call int32dateGetMinute
mov r1 2
call putdecpad
mov r0 ':'
call putc0
mov r0 currDate
call int32dateGetSecond
mov r1 2
call putdecpad

mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
