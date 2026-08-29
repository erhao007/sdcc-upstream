        .module spx2
        .area   MCS251CODE (ABS)
        .org    0x0000
        mov     spx, #0x100
        mov     a, #0x42
        mov     @spx, a          ; [0x100] = 0x42
        mov     a, #0x00
        mov     a, @spx          ; A = 0x42
        mov     wr4, @spx-0x0001 ; read 16-bit at 0x0ff
        inc     spx, #2
        push    acc
        mul     ab
spin:   sjmp    spin
