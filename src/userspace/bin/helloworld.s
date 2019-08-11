; include standard library files we need for puts0 and exit calls
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s

; define our message (as a constant)
db msg 'Hello world!\n',0

; print msg to stdout
mov r0 msg
call puts0

; exit
mov r0 0
call exit
