; MCS-251 ISA semantics v6 — ST-1S-A: register/accumulator transforms.
; Independent oracle from the public manuals (STC32G datasheet p.553,
; Intel 8XC251SB UM A-23/A-14; flag tables pp.1650/1651 resp. A-102/A-105):
;   PSW1 layout: bit7=CY, bit6=AC, bit5=N, bit4:3=RS1:RS0, bit2=OV,
;                bit1=Z, bit0=reserved; CY/AC/RS/OV mirror PSW.
;   RLC A:  A'=(A<<1)|CY, CY'=A.7; AC/OV preserved; N/Z per result
;   RRC A:  A'=(A>>1)|(CY<<7), CY'=A.0; AC/OV preserved; N/Z per result
;   RR  A:  A'=(A>>1)|(A.0<<7); CY/AC/OV preserved; N/Z per result
;   SWAP A: nibbles exchanged; CY/AC/OV preserved; N/Z per result
;   MOVS WRj,Rm: WRj=sign-extend(Rm); CY/AC/OV/N/Z ALL preserved
;   MOVZ WRj,Rm: WRj=zero-extend(Rm); CY/AC/OV/N/Z ALL preserved
; Flags are preset through "mov 0xd1,#v" (RS kept 0) and the FULL PSW1
; byte is captured after each op ("mov a,0xd1") — this asserts both the
; documented updates and the preservation of every unaffected flag,
; plus the CY/AC/RS/OV mirror with PSW.  A bit-store of C gives an
; independent PSW-side CY witness.  Layout, 3 bytes per case
; (A or word hi/lo, full PSW1, CY witness), IRAM 0x50..0x79:
;   0x50-52 RLC ps=0xC4 A=0x85 | 0x53-55 RLC ps=0x44 A=0x55
;   0x56-58 RLC ps=0x44 A=0x00 | 0x59-5b RRC ps=0xC4 A=0x0B
;   0x5c-5e RRC ps=0x44 A=0x86 | 0x5f-61 RRC ps=0x44 A=0x00
;   0x62-64 RR  ps=0xC4 A=0x81 | 0x65-67 RR  ps=0xC4 A=0x00
;   0x68-6a SWAP ps=0xC4 A=0xC3| 0x6b-6d SWAP ps=0xC4 A=0x00
;   0x6e-70 MOVS ps=0xE6 r3=0x80 | 0x71-73 MOVS ps=0xE6 r3=0x7F
;   0x74-76 MOVZ ps=0xE6 r3=0xFF | 0x77-79 MOVZ ps=0xE6 r3=0x00
        .module isasem6
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        mov     spx, #0x0200
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
        mov     0x63, #0x00
        mov     0x64, #0x00
        mov     0x65, #0x00
        mov     0x66, #0x00
        mov     0x67, #0x00
        mov     0x68, #0x00
        mov     0x69, #0x00
        mov     0x6a, #0x00
        mov     0x6b, #0x00
        mov     0x6c, #0x00
        mov     0x6d, #0x00
        mov     0x6e, #0x00
        mov     0x6f, #0x00
        mov     0x70, #0x00
        mov     0x71, #0x00
        mov     0x72, #0x00
        mov     0x73, #0x00
        mov     0x74, #0x00
        mov     0x75, #0x00
        mov     0x76, #0x00
        mov     0x77, #0x00
        mov     0x78, #0x00
        mov     0x79, #0x00

        ; === RLC A: CY'=A.7, A'=(A<<1)|CY; AC/OV preserved ===
        mov     0xd1, #0xc4       ; CY=1 AC=1 OV=1 (RS=0)
        mov     a, #0x85
        rlc     a                 ; -> A=0x0B, CY=1, N=0, Z=0
        mov     0x50, a
        mov     a, 0xd1
        mov     0x51, a           ; expect 0xC4 (AC/OV preserved)
        mov     0x52.0, c         ; CY witness = 1

        mov     0xd1, #0x44       ; CY=0 AC=1 OV=1
        mov     a, #0x55
        rlc     a                 ; -> A=0xAA, CY=0, N=1, Z=0
        mov     0x53, a
        mov     a, 0xd1
        mov     0x54, a           ; expect 0x64
        mov     0x55.0, c         ; 0

        mov     0xd1, #0x44
        mov     a, #0x00
        rlc     a                 ; -> A=0x00, CY=0, N=0, Z=1
        mov     0x56, a
        mov     a, 0xd1
        mov     0x57, a           ; expect 0x46
        mov     0x58.0, c         ; 0

        ; === RRC A: CY'=A.0, A'=(A>>1)|(CY<<7); AC/OV preserved ===
        mov     0xd1, #0xc4       ; CY=1 AC=1 OV=1
        mov     a, #0x0b
        rrc     a                 ; -> A=0x85, CY=1, N=1, Z=0
        mov     0x59, a
        mov     a, 0xd1
        mov     0x5a, a           ; expect 0xE4
        mov     0x5b.0, c         ; 1

        mov     0xd1, #0x44
        mov     a, #0x86
        rrc     a                 ; -> A=0x43, CY=0, N=0, Z=0
        mov     0x5c, a
        mov     a, 0xd1
        mov     0x5d, a           ; expect 0x44
        mov     0x5e.0, c         ; 0

        mov     0xd1, #0x44
        mov     a, #0x00
        rrc     a                 ; -> A=0x00, CY=0, N=0, Z=1
        mov     0x5f, a
        mov     a, 0xd1
        mov     0x60, a           ; expect 0x46
        mov     0x61.0, c         ; 0

        ; === RR A: CY/AC/OV preserved, N/Z per result ===
        mov     0xd1, #0xc4       ; CY=1 AC=1 OV=1
        mov     a, #0x81
        rr      a                 ; -> A=0xC0, N=1, Z=0; CY/AC/OV kept
        mov     0x62, a
        mov     a, 0xd1
        mov     0x63, a           ; expect 0xE4
        mov     0x64.0, c         ; 1 (CY preserved)

        mov     0xd1, #0xc4
        mov     a, #0x00
        rr      a                 ; -> A=0x00, N=0, Z=1; flags kept
        mov     0x65, a
        mov     a, 0xd1
        mov     0x66, a           ; expect 0xC6
        mov     0x67.0, c         ; 1 (CY preserved)

        ; === SWAP A: nibbles exchanged; CY/AC/OV preserved ===
        mov     0xd1, #0xc4
        mov     a, #0xc3
        swap    a                 ; -> A=0x3C, N=0, Z=0; CY/AC/OV kept
        mov     0x68, a
        mov     a, 0xd1
        mov     0x69, a           ; expect 0xC4
        mov     0x6a.0, c         ; 1 (CY preserved)

        mov     0xd1, #0xc4
        mov     a, #0x00
        swap    a                 ; -> A=0x00, N=0, Z=1; flags kept
        mov     0x6b, a
        mov     a, 0xd1
        mov     0x6c, a           ; expect 0xC6
        mov     0x6d.0, c         ; 1 (CY preserved)

        ; === MOVS WRj,Rm: sign-extend; ALL flags preserved ===
        mov     0xd1, #0xe6       ; CY=1 AC=1 N=1 OV=1 Z=1
        mov     r3, #0x80
        movs    wr4, r3           ; -> wr4 = 0xFF80; flags stay 0xE6
        mov     0x6e, r4
        mov     0x6f, r5
        mov     a, 0xd1
        mov     0x70, a           ; expect 0xE6 (full preservation)

        mov     0xd1, #0xe6
        mov     r3, #0x7f
        movs    wr4, r3           ; -> wr4 = 0x007F; flags stay 0xE6
        mov     0x71, r4
        mov     0x72, r5
        mov     a, 0xd1
        mov     0x73, a           ; expect 0xE6

        ; === MOVZ WRj,Rm: zero-extend; ALL flags preserved ===
        mov     0xd1, #0xe6
        mov     r3, #0xff
        movz    wr6, r3           ; -> wr6 = 0x00FF; flags stay 0xE6
        mov     0x74, r6
        mov     0x75, r7
        mov     a, 0xd1
        mov     0x76, a           ; expect 0xE6

        mov     0xd1, #0xe6
        mov     r3, #0x00
        movz    wr6, r3           ; -> wr6 = 0x0000; flags stay 0xE6
        mov     0x77, r6
        mov     0x78, r7
        mov     a, 0xd1
        mov     0x79, a           ; expect 0xE6

spin:   sjmp    spin
