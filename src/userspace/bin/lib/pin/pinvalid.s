db pinvalidArray 255,255,255,143,59,255,39,123,0,3,255,255 ; 8 pins per byte, lower pins towards LSB

label pinvalid ; num=r0, returns boolean result in r0
; Outside of valid range? (outside of [0,95])
mov r1 0
cmp r1 r0 r1
skipge r1
jmp pinvalidFalse
mov r1 95
cmp r1 r0 r1
skiple r1
jmp pinvalidFalse
; Now lookup in array
mov r1 3 ; compute array index (pinNum/8)
shr r1 r0 r1
mov r2 pinvalidArray ; grab relevant byte from array
add r1 r1 r2
load8 r1 r1
mov r2 7 ; compute shift within byte (pinNum%8)
and r2 r0 r2
shr r1 r1 r2 ; shift loaded byte
mov r2 1 ; mask away other bits ready to return
and r0 r1 r2
ret
; Return false case
label pinvalidFalse
mov r0 0
ret
