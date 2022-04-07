require ../../sys/syscall.s

requireend ../int32/int32add.s
requireend ../int32/int32set.s

aw fgets32ScratchInt 2

; r=gets0(buf=r0, len=r1)=fgets(stdin, 0, buf, len)
label gets0
; Prepare registers for fgets
mov r2 r0
mov r3 r1
mov r0 FdStdin
mov r1 0
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
; Check for failure (note r0 can only be 0 or 1 so can simply inspect lowest bit)
skip0 r0
jmp fgetsLoopEnd
; Check for newline
load8 r4 r3
inc r3 ; we can advance r3 now as we will need to do it in either case
mov r0 '\n'
cmp r0 r4 r0
skipneq r0
jmp fgetsLoopEnd
; No newline, prepare for next iteration
inc r2 ; inc read offset
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

; r=fgets32(fd=r0, offsetPtr=r1, buf=r2, len=r3) reads up to and including first newline, always null-terminates buf (potentially to be 0 length if could not read), returns number of bytes read from file
; TODO: Support length limit from r3
label fgets32
; Init - copy offset value into scratch int
push8 r0
push16 r2
push16 r3
mov r0 fgets32ScratchInt
call int32set32
pop16 r3
pop16 r2
pop8 r0
; Init - move some values around to match read syscall better
push16 r2 ; save initial buffer pointer for length calculation later
mov r3 r2 ; move buffer into r3 for read syscall
mov r1 r0 ; move fd into r1 for read syscall
; Start of reading loop
label fgets32LoopStart
; Read single character into user provided buffer
mov r0 SyscallIdRead32
mov r2 fgets32ScratchInt
mov r4 1
syscall
; Check for failure (note r0 can only be 0 or 1 so can simply inspect lowest bit)
skip0 r0
jmp fgets32LoopEnd
; Check for newline
load8 r4 r3
inc r3 ; we can advance r3 now as we will need to do it in either case
mov r0 '\n'
cmp r0 r4 r0
skipneq r0
jmp fgets32LoopEnd
; No newline, prepare for next iteration by incrementing offset
push8 r1
push16 r2
push16 r3
mov r0 fgets32ScratchInt
call int32inc
pop16 r3
pop16 r2
pop8 r1
jmp fgets32LoopStart
; End of reading loop
label fgets32LoopEnd
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

; r0=fgetc32(fd=r0, offsetPtr=r1), returns 256 on failure
label fgetc32
; read single character onto stack
mov r2 r1
mov r1 r0
mov r0 SyscallIdRead32
mov r3 r6
inc r6
mov r4 1
syscall
; check for failure
cmp r0 r0 r0
skipeqz r0
jmp fgetc32Done
; failed
dec r6 ; fix stack
mov r0 256
ret
; success
label fgetc32Done
pop8 r0 ; pop read character off stack
ret
