; MCS-251 simulator regression v2 (overlap-aware ordering)
        .module regress2
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        ; --- MOV dir8 / @Ri / Rn ---
        mov     0x30, #0x11
        mov     a, 0x30          ; A = 0x11
        mov     r2, a            ; R2 = 0x11
        mov     r3, 0x30         ; R3 = 0x11
        mov     0x31, a          ; [0x31] = 0x11
        mov     r0, #0x40        ; R0 = 0x40
        mov     @r0, #0x22       ; [0x40] = 0x22
        mov     a, @r0           ; A = 0x22
        mov     @r0, a           ; [0x40] = 0x22
        mov     @r0, 0x30        ; [0x40] = 0x11
        ; --- arithmetic/logic (result saved to 0x33 before WR/DR) ---
        mov     a, #0x0f
        add     a, #0x01         ; 0x10
        add     a, 0x30          ; 0x21
        anl     a, #0xf0         ; 0x20
        orl     a, #0x03         ; 0x23
        xrl     a, #0xff         ; 0xdc
        add     a, r2            ; 0xed (r2=0x11)
        subb    a, #0x10         ; 0xdd
        mov     0x33, a          ; save 0xdd
        inc     r5               ; r5=1
        dec     r5               ; r5=0
        inc     0x32             ; [0x32]=1
        ; --- WR/DR (R8-R15 region, no clash with R0-R3/R5) ---
        mov     wr4, #0x1234
        mov     wr6, wr4         ; WR6 = 0x1234
        mov     dr8, #0xabcd     ; #0data16: DR8 = 0x0000abcd
        mov     dr12, #-3        ; #1data16: DR12 = 0xfffffffd
        mov     dr12, dr8        ; DR12 = DR8 = 0x0000abcd
        ; --- flags ---
        clr     a                ; A = 0
        cpl     a                ; A = 0xff
        clr     c                ; CY = 0
        cpl     c                ; CY = 1
spin:   sjmp    spin
