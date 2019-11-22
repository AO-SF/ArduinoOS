requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/int32/int32time.s

aw currTime32 2 ; 32 bit time value

; Get time since booting and print it, followed by a newline
; Note: we use 32 bit library, allowing uptimes of around 137 years before rolling over
mov r0 currTime32
call int32gettimemonotonic
mov r0 currTime32
call int32puttime
mov r0 '\n'
call putc0

; Exit
mov r0 0
call exit
