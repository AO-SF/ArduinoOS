; int32getUpper16(src=r0) - places upper half of src into r0 - note: also ensures only r0 is used, so nothing else needs protecting
label int32getUpper16
load16 r0 r0
ret

; int32getLower16(src=r0) - places lower half of src into r0 - note: also ensures only r0 is used, so nothing else needs protecting
label int32getLower16
inc2 r0
load16 r0 r0
ret
