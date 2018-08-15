; startup program - called by init before dropping into a shell
; used to e.g. mount any external volumes such as sd cards
; for now simply do nothing and exit with success
mov r0 0
mov r1 0
syscall
