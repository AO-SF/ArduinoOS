requireend lib/std/int32/int32time.s
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

; Print in form d/m/y h:m:s
mov r0 currDate
call int32datePrint

mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
