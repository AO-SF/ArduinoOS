; Tile data entries are 16 bytes each,
; with the first (up to) 11 used for the colour sequence,
; then the next 5 (at most) are used for the UTF8 character, terminated with a null byte,
; with the 15th (final) byte used for flags
;
; Current tiles are: 0 - water, 1 - grass, 2 - tree.
const tileDataSize 16
db tileData 27,'[','3','4',';','1',';','4','4','m',226,150,147,0,0,0,	27,'[','3','2',';','1',';','4','2','m',226,150,145,0,0,0,	27,'[','3','3',';','4','2','m',240,159,140,178,0,0,0,0

label tilesGetPrintString ; takes tile id in r0, returns addr in r0
mov r1 tileDataSize
mul r0 r0 r1
mov r1 tileData
add r0 r0 r1
ret
