; this module is used to easily register a basic suicide handler for simple programs
; programs using it should 'require' it as one of the very first instructions (within first 256 bytes)

require ../../sys/sys.s

; register handler
mov r0 SyscallIdRegisterSignalHandler
mov r1 SignalIdSuicide
mov r2 suicideHandlerStart
syscall

; jump past handler
jmp suicideHandlerEnd

; handler - exit
label suicideHandlerStart
mov r0 SyscallIdExit
mov r1 1
syscall

; post handler - continue executing program
label suicideHandlerEnd
