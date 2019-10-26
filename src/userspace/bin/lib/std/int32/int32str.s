require int32common.s

requireend int32log.s

const int32toStrBufSize 12 ; includes space for null terminator and potential minus sign

aw int32toStrScratchInt32A 2
aw int32toStrScratchInt32B 2

; int32toStr(str=r0, x=r1) - write 32 bit value pointed to by x in decimal into given str buffer. buffer should hold at least int32toStrBufSize bytes
label int32toStr
; Copy x into scratch int so we can modify
push16 r0
mov r0 int32toStrScratchInt32A
call int32set32
pop16 r0
; Calculate log10(x)+1 to get length in decimal (log10 handles x=0 case in our favour)
push16 r0
mov r0 int32toStrScratchInt32A
call int32log10
inc r0
mov r1 r0
pop16 r0
; Move str pointer to the end and work backwards
add r0 r0 r1
; Add null terminator
mov r1 0
store8 r0 r1
dec r0
; Start of digit printing loop
label int32toStrLoopStart
; Divide x by 10 while making note of the remainder.
push16 r0
mov r0 int32toStrScratchInt32A
mov r1 Int32Const1E1
mov r2 int32toStrScratchInt32B
call int32div32rem
mov r0 int32toStrScratchInt32B
call int32getLower16
mov r1 r0
pop16 r0
; Print digit to str (str ptr in r0, digit in r1)
inc48 r1
store8 r0 r1
dec r0
; If remaining value is greater than 0, loop again to print next digit
push16 r0
mov r0 int32toStrScratchInt32A
mov r1 Int32Const0
call int32Equal
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp int32toStrLoopStart
; Done
ret
