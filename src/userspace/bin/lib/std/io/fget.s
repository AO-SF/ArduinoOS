require ../../sys/syscall.s

; r=gets0(buf=r0, len=r1)=fgets(stdin, 0, buf, len)
label gets0
; Prepare registers for fgets
mov r2 r0
mov r3 r1
mov r1 0
; Load stdin fd into r0
mov r0 SyscallIdEnvGetStdinFd
syscall
; Jump to fgets to do most of the work
jmp fgets ; ret in fgets will return us correctly

; r=fgets(fd=r0, offset=r1, buf=r2, len=r3) reads up to and including first newline, always null-terminates buf (potentially to be 0 length if could not read), returns number of bytes read from file
; TODO: Support length limit from r3
label fgets
; Init - move some values around to match read syscall better
push16 r2 ; save initial buffer pointer for length calculation later
mov r3 r2 ; move buffer into r3 for read syscall
mov r2 r1 ; move offset into r2 for read syscall
mov r1 r0 ; move fd into r1 for read syscall
; Start of reading loop
label fgetsLoopStart
; Read single character into user provided buffer
mov r0 SyscallIdRead
mov r4 1
syscall
; Check for failure
cmp r0 r0 r0
skipeq r0
jmp fgetsLoopEnd
; Check for newline
load8 r4 r3
mov r0 '\n'
cmp r0 r4 r0
skipeq r0
jmp fgetsLoopNext ; no newline, prepare for next iteration
; Newline
inc r3 ; advance buffer pointer so null terminator does not overwrite it
jmp fgetsLoopEnd
; Loop to read next character
label fgetsLoopNext
inc r2 ; inc read offset
inc r3 ; advance buffer pointer
jmp fgetsLoopStart
; End of reading loop
label fgetsLoopEnd
; Add null terminator
mov r0 0
store8 r3 r0
; Return length
pop16 r4 ; pop original buffer pointer from stack
sub r0 r3 r4 ; subtract original from current
ret

; r0=fgetc(fd=r0, offset=r1), returns 256 on failure
label fgetc
; read single character onto stack
mov r2 r1
mov r1 r0
mov r0 SyscallIdRead
mov r3 r6
inc r6
mov r4 1
syscall
; check for failure
cmp r0 r0 r0
skipeqz r0
jmp fgetcDone
; failed
dec r6 ; fix stack
mov r0 256
ret
; success
label fgetcDone
pop8 r0 ; pop read character off stack
ret
