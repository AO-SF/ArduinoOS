jmp libsysthreadend

; Locks are boolean 'taken' flags (stored in 8 bits), using the xchg8 instruction where necessary to be atomic.
; To setup a lock, reserve a byte in memory and set it to 0 to make it initially unlocked, or to 1 to be initially locked,
; then pass its address to the functions below to use it.

label lockwait ; r0=lock ptr, busy-waits trying to grab lock
push16 r0
label lockwaitloopstart
pop16 r0
push16 r0
call lockwaittry
cmp r0 r0 r0
skipneqz r0
jmp lockwaitloopstart
pop16 r0 ; restore stack
ret

label lockwaittry ; r0=lock ptr, tries to grab lock returning immediately. If lock was taken 1 is returned, otherwise 0.
; swap 1 into lock to either take or preserve taken status
mov r1 1
xchg8 r0 r1
; if lock was free, xchg returns 0, if lock was taken, xchg returns 1,
; so xor to invert the result ready to return
mov r0 1
xor r0 r1 r0
ret

label lockpost ; r0=lock ptr, releases lock
; simply clear lock to 0 in standard way
mov r1 0
store8 r0 r1
ret

label libsysthreadend
