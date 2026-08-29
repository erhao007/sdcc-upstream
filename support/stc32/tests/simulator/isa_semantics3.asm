; MCS-251 ISA semantics v3 — A9 extended bit ops, A5 legacy XCH/XCHD/CJNE@Ri,
; DA, shifts, CMP WR/DR, MOVH, dir16 MOV/ALU, @WRj ALU, LJMP@WRj, ACALL.
; Every result lands in IRAM 0x50..0x6a for post-run assertions
; (tools/run_isa_semantics.py compares the dump automatically).
        .module isasem3
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        mov     spx, #0x0200      ; 251 stack lives at SPX, not legacy SP
        mov     0x50, #0x00
        mov     0x51, #0x00
        mov     0x52, #0x00
        mov     0x53, #0x00
        mov     0x54, #0x00
        mov     0x55, #0x00
        mov     0x56, #0x00
        mov     0x57, #0x00
        mov     0x58, #0x00
        mov     0x59, #0x00
        mov     0x5a, #0x00
        mov     0x5b, #0x00
        mov     0x5c, #0x00
        mov     0x5d, #0x00
        mov     0x5e, #0x00
        mov     0x5f, #0x00
        mov     0x60, #0x00
        mov     0x61, #0x00
        mov     0x62, #0x00

        ; === A9 extended bit operations (byte 0x50, initial 0x00) ===
        setb    0x50.4            ; 0x50 = 0x10
        setb    0x50.6            ; 0x50 = 0x50
        clr     0x50.6            ; 0x50 = 0x10
        cpl     0x50.5            ; 0x50 = 0x30
        cpl     0x50.5            ; 0x50 = 0x10 (back)
        setb    0x50.5            ; 0x50 = 0x30
        mov     c, 0x50.4         ; CY=1
        anl     c, 0x50.5         ; CY = 1&1 = 1
        mov     0x5a.0, c         ; 0x5a = 0x01
        clr     0x50.5            ; 0x50 = 0x10
        mov     c, 0x50.5         ; CY = 0
        orl     c, 0x50.4         ; CY = 0|1 = 1
        anl     c, /0x50.5        ; CY = 1&!0 = 1
        mov     0x5b.0, c         ; 0x5b = 0x01
        mov     c, 0x50.5         ; CY = 0
        orl     c, /0x50.6        ; CY = 0 | !0 = 1
        mov     0x5c.0, c         ; 0x5c = 0x01
        mov     0x50.7, c         ; bit7 from CY -> 0x50 = 0x90

        ; === JB / JNB / JBC two paths (bit 0x51.2) ===
        jb      0x51.2, sk1       ; not set -> fall through
        inc     0x52              ; 0x52 = 0x01
sk1:    setb    0x51.2            ; 0x51 = 0x04
        jnb     0x51.2, sk2       ; set -> fall through
        inc     0x52              ; 0x52 = 0x02
sk2:    jbc     0x51.2, sk3       ; set -> jump AND clear (0x51=0x00)
        inc     0x52              ; must NOT execute
sk3:    mov     r2, 0x51          ; R2 = 0x00 (bit cleared by JBC)
        inc     0x52              ; 0x52 = 0x03

        ; === XCH A,@Ri / XCHD A,@Ri ===
        mov     r1, #0x53
        mov     0x53, #0x55
        mov     a, #0xaa
        xch     a, @r1            ; A=0x55, [0x53]=0xaa
        mov     0x54, a           ; 0x54 = 0x55
        mov     a, #0xab
        xchd    a, @r1            ; swap low nibbles: A=0xaa, [0x53]=0xab
        mov     0x55, a           ; 0x55 = 0xaa

        ; === DA A boundaries ===
        mov     a, #0x9a
        da      a                 ; A=0x00, CY=1
        mov     0x56, a           ; 0x56 = 0x00
        mov     0x57.0, c         ; 0x57 = 0x01
        mov     a, #0x45
        da      a                 ; A=0x45, CY=0
        mov     0x58, a           ; 0x58 = 0x45

        ; === shifts: SRA keeps sign, CY = shifted-out ===
        mov     r10, #0xf0
        sra     r10               ; R10=0xf8, CY=0
        mov     0x59, r10         ; 0x59 = 0xf8
        mov     wr6, #0x8001
        srl     wr6               ; WR6=0x4000, CY=1
        mov     0x5d.0, c         ; 0x5d = 0x01
        mov     wr6, #0x4000
        sll     wr6               ; WR6=0x8000, CY = original MSB(0x4000)=0
        ; CMP WR
        mov     wr4, #0x1234
        mov     wr6, #0x1234
        cmp     wr4, wr6          ; equal: Z=1, CY=0
        mov     wr4, #0x0001
        mov     wr6, #0x0002
        cmp     wr4, wr6          ; less: CY=1

        ; === MOVH: high half only ===
        mov     dr8, #0xabcd      ; DR8 = 0x0000abcd
        movh    dr8, #0x1234      ; DR8 = 0x1234abcd
        ; === dir16 store/load round-trip ===
        mov     wr6, #0xbeef
        mov     0x0060, wr6       ; [0x60..0x61] = be ef
        mov     wr4, #0x0000
        mov     wr4, 0x0060       ; WR4 = 0xbeef
        ; === ADD Rm,@WRj (3-byte corrected form) ===
        mov     wr6, #0x0062
        mov     0x62, #0x0f
        mov     r12, #0x01
        .db     0x2e, 0x39, 0xc0  ; ADD R12,@WR6
        mov     0x5e, r12         ; 0x5e = 0x10
        ; === LJMP @WRj ===
        mov     wr6, #jmpback
        ljmp    @wr6
        sjmp    fail1
jmpback:
        inc     0x5f              ; 0x5f = 0x01

        ; === ACALL: return address on the spx stack ===
        acall   sub1
        cjne    r2, #0x77, fail1

        ; === native PUSH/POP byte-order round trips ===
        mov     spx, #0x0300
        push    #0xabcd
        pop     wr4
        mov     0x63, wr4             ; 0x63..0x64 = ab cd
        mov     wr6, #0x1234
        push    wr6
        mov     wr6, #0x0000
        pop     wr6
        mov     0x65, wr6             ; 0x65..0x66 = 12 34
        mov     dr8, #0x5678
        movh    dr8, #0x1234
        push    dr8
        mov     dr8, #0x0000
        pop     dr8
        mov     0x67, dr8             ; 0x67..0x6a = 12 34 56 78

spin:   sjmp    spin

fail1:  mov     0x5f, #0xff
        sjmp    spin

sub1:   mov     r2, #0x77
        ret
