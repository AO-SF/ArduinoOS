require int32common.s

requireend int32add.s
requireend int32div.s
requireend int32get.s
requireend int32log.s
requireend int32mul.s
requireend int32set.s

const int32toStrBufSize 12 ; includes space for null terminator and potential minus sign

aw int32toStrScratchInt32 2

; int32toStr(str=r0, x=r1) - write 32 bit value pointed to by x in decimal into given str buffer. buffer should hold at least int32toStrBufSize bytes
label int32toStr
; Copy x into scratch int so we can modify
push16 r0
mov r0 int32toStrScratchInt32
call int32set32
pop16 r0
; Calculate log10(x)+1 to get length in decimal (log10 handles x=0 case in our favour)
push16 r0
mov r0 int32toStrScratchInt32
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
mov r0 int32toStrScratchInt32
mov r1 10
call int32div16
mov r1 r0
pop16 r0
; Print digit to str (str ptr in r0, digit in r1)
inc48 r1
store8 r0 r1
dec r0
; If remaining value is greater than 0, loop again to print next digit
push16 r0
mov r0 int32toStrScratchInt32
mov r1 Int32Const0
call int32Equal
cmp r1 r0 r0
pop16 r0
skipneqz r1
jmp int32toStrLoopStart
; Done
ret

; int32fromStr(x=r0, str=r1) - attempt to parse given str as a 32 bit integer, stores 0 in x on failure
label int32fromStr
; Set x=0
push16 r0
push16 r1
mov r1 0
call int32set16
pop16 r1
pop16 r0
; Loop until we hit a character outside of '0'-'9' range
label int32fromStrLoopStart
; Subtract '0' from char and test if in valid range
load8 r2 r1
dec48 r2
mov r3 0
cmp r3 r2 r3
skipge r3
jmp int32fromStrLoopEnd
mov r3 9
cmp r3 r2 r3
skiple r3
jmp int32fromStrLoopEnd
; Append this digit to running total
push16 r1
push16 r0
push8 r2
mov r1 Int32Const1E1
call int32mul32
pop8 r1
pop16 r0
push16 r0
call int32add16
pop16 r0
pop16 r1
; Loop to try next character
inc r1
jmp int32fromStrLoopStart
label int32fromStrLoopEnd
ret
