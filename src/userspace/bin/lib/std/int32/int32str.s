const int32toStrBufSize 12 ; includes space for null terminator and potential minus sign

; int32toStr(str=r0, x=r1) - write 32 bit value pointed to by x in decimal into given str buffer. buffer should hold at least int32toStrBufSize bytes
label int32toStr
; TODO: this
; Add null terminator
label temptemp
mov r1 0
store8 r0 r1
; Return
ret
