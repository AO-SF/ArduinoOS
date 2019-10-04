require pindef.s
requireend ../std/str/strtoint.s

const strtopinAnalogueCount 16
db strtopinAnalogueArray 40,41,42,43,44,45,46,47,80,81,82,83,84,85,86,87 ; A0-A15

const strtopinDigitalCount 54
db strtopinDigitalArray 32,33,36,37,53,35,59,60,61,62,12,13,14,15,73,72,57,56,27,26,25,24,0,1,2,3,4,5,6,7,23,22,21,20,19,18,17,16,31,50,49,48,95,94,93,92,91,90,89,88,11,10,9,8 ; D0-D53

; str=r0, returns integer result in r0
; accepts strings of the following forms:
; * raw integers (0-128 although not all valid)
; * Ax (x in range [0,15])
; * Dx (x in range [0,53])
label strtopin
; Check for analogue prefix
load8 r1 r0
mov r2 'A'
cmp r2 r1 r2
skipneq r2
jmp strtopinAnalogue
mov r2 'a'
cmp r2 r1 r2
skipneq r2
jmp strtopinAnalogue
; Check for digital prefix
mov r2 'D'
cmp r2 r1 r2
skipneq r2
jmp strtopinDigital
mov r2 'd'
cmp r2 r1 r2
skipneq r2
jmp strtopinDigital
; Otherwise assume raw integer
call strtoint
ret
; Try as analogue pin
label strtopinAnalogue
inc r0 ; skip 'A'
call strtoint
; Check num within valid range
mov r1 strtopinAnalogueCount
cmp r1 r0 r1
skiplt r1
jmp strtopinBadPin
; Load raw pin number from array
mov r1 strtopinAnalogueArray
add r0 r0 r1
load8 r0 r0
ret
; Try as digital pin
label strtopinDigital
inc r0 ; skip 'D'
call strtoint
; Check num within valid range
mov r1 strtopinDigitalCount
cmp r1 r0 r1
skiplt r1
jmp strtopinBadPin
; Load raw pin number from array
mov r1 strtopinDigitalArray
add r0 r0 r1
load8 r0 r0
ret
; Bad value (out of array bounds)
label strtopinBadPin
mov r0 PinInvalid
ret
