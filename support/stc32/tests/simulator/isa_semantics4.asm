; MCS-251 ISA semantics v4 — MUL/DIV Rmd,Rms (UM A-8 allocation rules),
; wide-ALU flags (CY/OV/AC per UM A-13), JZ/JNZ accumulator semantics,
; N/Z flag boundaries, and STC32G TRAP=NOP semantics.
; Results land in IRAM 0x40..0x7F; each flag cell holds 0x00 or 0x01.
        .module isasem4
        .area   MCS251CODE (ABS)
        .org    0x0000
        ljmp    main

        .org    0x0100
main:
        mov     sp, #0x7f
        mov     spx, #0x0300
        .rept   0x13
        .endm
        mov     0x40, #0x00
        mov     0x41, #0x00
        mov     0x42, #0x00
        mov     0x43, #0x00
        mov     0x44, #0x00
        mov     0x45, #0x00
        mov     0x46, #0x00
        mov     0x30, #0x00
        mov     0x31, #0x00
        mov     0x32, #0x00
        mov     0x33, #0x00
        mov     0x34, #0x00
        mov     0x35, #0x00
        mov     0x36, #0x00
        mov     0x37, #0x00
        mov     0x38, #0x00
        mov     0x39, #0x00
        mov     0x3a, #0x00
        mov     0x3b, #0x00
        mov     0x3c, #0x00
        mov     0x3d, #0x00
        mov     0x3e, #0x00
        mov     0x3f, #0x00
        mov     0x40, #0x00
        mov     0x41, #0x00
        mov     0x42, #0x00
        mov     0x43, #0x00
        mov     0x44, #0x00
        mov     0x45, #0x00
        mov     0x46, #0x00
        mov     0x47, #0x00
        mov     0x48, #0x00
        mov     0x49, #0x00
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
        mov     0x7a, #0x00
        mov     0x7b, #0x00
        mov     0x7c, #0x00
        mov     0x7d, #0x00
        mov     0x7e, #0x00
        mov     0x7f, #0x00
        mov     0x80, #0x00
        mov     0x81, #0x00
        mov     0x82, #0x00
        mov     0x83, #0x00

        ; === MUL Rmd,Rms (UM A-101 example: R1=0x50, R0=0xA0) ===
        mov     r0, #0xa0
        mov     r1, #0x50
        mul     r1, r0                  ; product 0x3200, md=1 (odd):
        mov     0x60, r1                ;  R1 = low  = 0x00
        mov     0x61, r0                ;  R0 = high = 0x32
        mov     a, 0xd1
        anl     a, #0x22                ; MUL N/Z: high byte nonzero, full result nonzero
        mov     0x45, a                 ; 0x45 = 0x20 (N)
        mov     0x62.0, c                 ; CY = 0
        ; OV=1 (product > 0xFF): probe via JB on PSW OV bit (0xD0.2)
        mov     c, psw.2
        mov     0x63.0, c              ; 0x63 = 0x01 if OV set

        ; MUL even md: R4=0x0F, R5=0x0F -> 0x00E1, R4=high=0x00, R5=low=0xE1
        mov     r4, #0x0f
        mov     r5, #0x0f
        mul     r4, r5
        mov     0x64, r4                ; 0x64 = 0x00
        mov     0x65, r5                ; 0x65 = 0xe1

        ; === DIV Rmd,Rms (UM A-55 example: R1=251, R5=18) ===
        mov     r1, #251
        mov     r5, #18
        div     r1, r5                  ; quotient 13, remainder 17, md=1:
        mov     0x66, r1                ;  R1 = quotient = 13 (0x0d)
        mov     0x67, r0                ;  R0 (Rmd-1) = remainder = 17 (0x11)

        ; DIV even md: R4=200, R5=7 -> quotient 28, remainder 4
        mov     r4, #200
        mov     r5, #7
        div     r4, r5                  ; md=4 even: R4=remainder, R5=quotient
        mov     0x68, r4                ; 0x68 = 4
        mov     0x69, r5                ; 0x69 = 28 (0x1c)

        ; === wide-ALU flags: ADD WR, 0x7FFF+1 -> 0x8000: CY=0 OV=1 N=1 Z=0 ===
        mov     wr4, #0x7fff
        mov     wr6, #0x0001
        add     wr4, wr6
        mov     0x6a.0, c                 ; CY = 0
        mov     c, psw.2
        mov     0x6b.0, c              ; OV = 1

        ; === byte ADD with carry+AC: 0xF0+0x10 -> 0x00: CY=1 AC=1 OV=0 Z=1 ===
        mov     r10, #0xf0
        add     r10, #0x10
        mov     0x6c.0, c                 ; CY = 1
        mov     c, psw.6
        mov     0x6d.0, c              ; AC = 1

        ; === SUB WR borrow: 0-1 -> 0xFFFF: CY=1 ===
        mov     wr4, #0x0000
        mov     wr6, #0x0001
        sub     wr4, wr6
        mov     0x6e.0, c                 ; CY = 1

        ; === JZ/JNZ test ACC (STC32G UM 1632) ===
        mov     a, #0x00
        jz      jzok                    ; A=0 -> taken
        inc     0x6f                    ; must not run
jzok:   inc     0x70                    ; 0x70 = 0x01
        mov     a, #0xff
        jnz     jnzok                   ; A!=0 -> taken
        inc     0x6f                    ; must not run
jnzok:  inc     0x71                    ; 0x71 = 0x01

        ; === TRAP: STC32G defines it as a NOP ===
        trap
        inc     0x72                    ; 0x72 = 0x01 after fall-through

        ; === legacy INC A / RL A set N,Z (UM A-13, unlike the 8051) ===
        ; JE/JNE (0x68/0x78) test the PSW1 Z flag directly, unlike JZ/JNZ
        ; which test ACC per the STC32G manual — so these assertions
        ; really cover the INC/RL flag update, not the accumulator value.
        mov     a, #0xff
        inc     a                       ; 0x00 -> Z=1
        je      incaok
        inc     0x6f                    ; must not run
incaok: inc     0x73                    ; 0x73 = 0x01
        mov     a, #0x40
        rl      a                       ; 0x80 -> N=1, Z=0
        je      rlbad                   ; Z=0 -> must not jump
        jsl     rlok                     ; N=1, OV=0 -> signed < : jump
rlbad:  inc     0x6f
rlok:   inc     0x74                    ; 0x74 = 0x01 (via jsl: PSW1 N)

        ; === 16-bit MUL WR (UM A-101: jd%4==0 -> WRjd=high, WRjd+2=low) ===
        mov     wr4, #300
        mov     wr6, #300
        mul     wr4, wr6                ; 90000 = 0x15F90 -> WR4=0x0001, WR6=0x5F90
        mov     0x75, wr4               ; 0x75..0x76 = 00 01
        mov     0x77, wr6               ; 0x77..0x78 = 5f 90
        mov     a, 0xd1
        anl     a, #0x22
        mov     0x46, a                 ; 0x46 = 0x20 (N, high word nonzero)
        ; === 16-bit DIV WR (UM A-55: jd%4==0 -> WRjd=remainder, WRjd+2=quotient) ===
        mov     wr4, #1000
        mov     wr6, #7
        div     wr4, wr6                ; 1000/7 = 142 rem 6 -> WR4=6, WR6=142
        mov     0x79, wr4               ; 0x79..0x7a = 00 06
        mov     0x7b, wr6               ; 0x7b..0x7c = 00 8e
        ; === SLL CY = original MSB (UM A-120) ===
        mov     wr6, #0x8000
        sll     wr6                     ; -> 0x0000, CY = 1
        mov     0x7d.0, c               ; 0x7d = 01
        ; === JLE/JG unsigned (UM A-14: JLE = Z||CY, JG = !Z&&!CY) ===
        mov     wr4, #1
        mov     wr6, #2
        cmp     wr4, wr6                ; 1 < 2: CY=1, Z=0
        jg      jg1                     ; unsigned >: must NOT jump
        inc     0x7e                    ; 0x7e = 01
jg1:    jle     jle1                    ; unsigned <=: must jump
        inc     0x6f                    ; must not run
jle1:   mov     wr4, #5
        mov     wr6, #2
        cmp     wr4, wr6                ; 5 > 2: CY=0, Z=0
        jle     jle2                    ; must NOT jump
        inc     0x7f                    ; 0x7f = 01
jle2:

        ; === logic ops leave CY unchanged (UM A-13: N/Z only) ===
        ; immediate forms (0x4E/0x5E/0x6E) go through exec_alu_rm, the
        ; function whose flag block was fixed — "orl wr4,wr6" (0x4D)
        ; would take exec_reg_alu and never exercise this path.
        mov     wr4, #0x1234
        setb    c                       ; CY=1
        orl     wr4, #0x0f0f            ; exec_alu_rm: ORL, keep CY
        mov     0x33.0, c               ; 0x33 = 0x01
        clr     c                       ; CY=0
        anl     wr4, #0x0ff0            ; exec_alu_rm: ANL, keep CY
        mov     0x36.0, c               ; 0x36 = 0x00
        setb    c
        xrl     wr4, #0x00f0            ; exec_alu_rm: XRL, keep CY
        mov     0x37.0, c               ; 0x37 = 0x01
        ; === ADD WRj,dir8 reads a big-endian word at dir8..dir8+1 ===
        mov     0x48, #0x12
        mov     0x49, #0x34
        mov     wr6, #0x0000
        add     wr6, 0x48               ; WR6 = 0x1234 (two bytes)
        mov     0x34, wr6               ; 0x34..0x35 = 12 34

        ; === flag boundary regressions ===
        mov     a, #0xff
        add     a, #1                   ; 8-bit wrap must set Z
        jsle    addzok                  ; N=0, OV=0, Z=1
        inc     0x6f                    ; must not run
addzok: inc     0x40                    ; 0x40 = 0x01
        mov     a, #1
        clr     c
        subb    a, #1                   ; exact zero must set Z
        jsle    subzok
        inc     0x6f                    ; must not run
subzok: inc     0x41                    ; 0x41 = 0x01
        mov     r10, #0x80
        mov     r12, #0xff
        add     r10, r12                ; 0x80+0xff=0x7f, OV=1
        mov     c, psw.2
        mov     0x42.0, c               ; 0x42 = 0x01
        mov     r10, #0xc9
        mov     r12, #0x54
        cmp     r10, r12                ; subtraction OV must use dst^src^result MSB
        mov     c, psw.2
        mov     0x44.0, c               ; 0x44 = 0x01
        mov     a, #0
        setb    c
        addc    a, #0x0f                ; carry-in must participate in AC
        mov     c, psw.6
        mov     0x43.0, c               ; 0x43 = 0x01

spin:   sjmp    spin
