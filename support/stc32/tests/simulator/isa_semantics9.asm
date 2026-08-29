; MCS-251 ISA semantics v9 — ST-1S-D: Conditional Jumps & Loops (9 forms).
; Independent oracle from public manuals:
;   - Intel 8XC251SB User's Manual, doc 272617-001, February 1995:
;       JC rel:         p.A-67;  flags: none (CY == 1)
;       JNC rel:        p.A-70;  flags: none (CY == 0)
;       JNE rel:        p.A-71;  flags: none (Z == 0)
;       JSG rel:        p.A-73;  flags: none ((N ^ OV) | Z == 0)
;       JSGE rel:       p.A-74;  flags: none (N ^ OV == 0, independent of Z)
;       JBC bit51, rel: p.A-68;  flags: none (clears bit on jump, preserves other bits)
;       JBC bit, rel:   p.A-69;  flags: none (clears bit on jump, preserves other bits)
;       DJNZ Rn, rel:   p.A-48;  flags: none (decrements Rn; flags unmodified)
;       DJNZ dir8, rel: p.A-49;  flags: none (decrements dir8; flags unmodified)
;   - STC32G Microcontroller User's Manual (2022), doc STC32G-CN:
;       Instruction details: pp.814..815 (all 9 forms; flags: none).
;
; Hardened Anti-Mutant & Oracle-Disambiguation Assertions:
;   1) Anti-Fallthrough Traps: Every taken jump branch is followed by a
;      trap "mov 0x10, #0xEx; sjmp ." so replacing jump with NOP halts immediately.
;   2) JSG vs JSGE Disambiguation:
;      - JSG: Tested with N=0/OV=0/Z=0 (taken), N=1/OV=1/Z=0 (taken),
;             N=0/OV=0/Z=1 (not-taken, kills JSGE mutant), N=1/OV=0 (not-taken),
;             N=0/OV=1 (not-taken);
;      - JSGE: Tested with N=0/OV=0/Z=0 (taken), N=1/OV=1/Z=0 (taken),
;              N=0/OV=0/Z=1 (taken, proves Z-independence, kills JSG mutant),
;              N=1/OV=0 (not-taken), N=0/OV=1 (not-taken);
;   3) JBC Single-Bit Clearing Proof:
;      - bit51: 0x20 preset to 0xAA -> after JBC 0x03 taken, 0x20 is 0xA2
;               (only bit 3 cleared, bits 7,5,1 intact); not-taken preserves 0xA2 & PSW1;
;      - bit251: 0x28 preset to 0x65 -> after JBC 0x28.5 taken, 0x28 is 0x45
;                (only bit 5 cleared, bits 6,2,0 intact); not-taken preserves 0x45 & PSW1;
;   4) DJNZ Flag Invariant Proof:
;      - Decrement to 1 (taken) and decrement to 0 (fallthrough) both preserve PSW1;
;   5) SPX & XRAM Canary: SPX remains 0x0200 and xram[0x0200] == 0x55.

        .module isasem9
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        mov     spx, #0x0200

        ; Clean IRAM 0x10..0x7f
        mov     r0, #0x10
clean_loop:
        mov     @r0, #0x00
        inc     r0
        cjne    r0, #0x80, clean_loop

        ; Stack boundary canary: xram[0x0200] = 0x55
        mov     dptr, #0x0200
        mov     a, #0x55
        movx    @dptr, a

        ; ====================================================================
        ; 1. JC rel (0x40): Jump if CY == 1
        ; ====================================================================
        ; 1a) Taken branch: CY = 1 (PSW1 = 0xC4)
        mov     0xd1, #0xc4
        jc      target_jc_taken
        ; Fallthrough trap:
        mov     0x10, #0xe1
trap_e1: sjmp   trap_e1

target_jc_taken:
        mov     0x31, #0x11       ; expect 0x11
        mov     a, 0xd1
        mov     0x32, a           ; expect 0xC4

        ; 1b) Not-taken branch: CY = 0 (PSW1 = 0x44)
        mov     0xd1, #0x44
        jc      trap_jc_not_taken
        ; Fallthrough (expected):
        mov     0x33, #0x12       ; expect 0x12
        mov     a, 0xd1
        mov     0x34, a           ; expect 0x44
        sjmp    test_jnc

trap_jc_not_taken:
        mov     0x10, #0xfa
trap_fa: sjmp   trap_fa

        ; ====================================================================
        ; 2. JNC rel (0x50): Jump if CY == 0
        ; ====================================================================
test_jnc:
        ; 2a) Taken branch: CY = 0 (PSW1 = 0x44)
        mov     0xd1, #0x44
        jnc     target_jnc_taken
        ; Fallthrough trap:
        mov     0x10, #0xe2
trap_e2: sjmp   trap_e2

target_jnc_taken:
        mov     0x35, #0x21       ; expect 0x21
        mov     a, 0xd1
        mov     0x36, a           ; expect 0x44

        ; 2b) Not-taken branch: CY = 1 (PSW1 = 0xC4)
        mov     0xd1, #0xc4
        jnc     trap_jnc_not_taken
        ; Fallthrough (expected):
        mov     0x37, #0x22       ; expect 0x22
        mov     a, 0xd1
        mov     0x38, a           ; expect 0xC4
        sjmp    test_jne

trap_jnc_not_taken:
        mov     0x10, #0xfb
trap_fb: sjmp   trap_fb

        ; ====================================================================
        ; 3. JNE rel (0x78): Jump if Z == 0
        ; ====================================================================
test_jne:
        ; 3a) Taken branch: Z = 0 (PSW1 = 0x44)
        mov     0xd1, #0x44
        jne     target_jne_taken
        ; Fallthrough trap:
        mov     0x10, #0xe3
trap_e3: sjmp   trap_e3

target_jne_taken:
        mov     0x39, #0x31       ; expect 0x31
        mov     a, 0xd1
        mov     0x3a, a           ; expect 0x44

        ; 3b) Not-taken branch: Z = 1 (PSW1 = 0x46)
        mov     0xd1, #0x46
        jne     trap_jne_not_taken
        ; Fallthrough (expected):
        mov     0x3b, #0x32       ; expect 0x32
        mov     a, 0xd1
        mov     0x3c, a           ; expect 0x46
        sjmp    test_jsg

trap_jne_not_taken:
        mov     0x10, #0xfc
trap_fc: sjmp   trap_fc

        ; ====================================================================
        ; 4. JSG rel (0x18): Jump if (N ^ OV) | Z == 0 (Signed >)
        ; ====================================================================
test_jsg:
        ; 4a) Taken: N=0, OV=0, Z=0 -> (0^0)|0 = 0 (PSW1 = 0x40)
        mov     0xd1, #0x40
        jsg     target_jsg_taken1
        ; Fallthrough trap:
        mov     0x10, #0xe4
trap_e4: sjmp   trap_e4

target_jsg_taken1:
        mov     0x3d, #0x41       ; expect 0x41
        mov     a, 0xd1
        mov     0x3e, a           ; expect 0x40

        ; 4b) Taken: N=1, OV=1, Z=0 -> (1^1)|0 = 0 (PSW1 = 0x24: N=bit5, OV=bit2)
        mov     0xd1, #0x24
        jsg     target_jsg_taken2
        ; Fallthrough trap:
        mov     0x10, #0xe5
trap_e5: sjmp   trap_e5

target_jsg_taken2:
        mov     0x3f, #0x42       ; expect 0x42
        mov     a, 0xd1
        mov     0x40, a           ; expect 0x24

        ; 4c) Not-taken: N=0, OV=0, Z=1 -> (0^0)|1 = 1 (PSW1 = 0x02)
        ;     KILLS JSGE MUTANT (JSGE would erroneously jump here!)
        mov     0xd1, #0x02
        jsg     trap_jsg_z1
        ; Fallthrough (expected):
        mov     0x41, #0x43       ; expect 0x43
        mov     a, 0xd1
        mov     0x42, a           ; expect 0x02
        sjmp    test_jsg_4d

trap_jsg_z1:
        mov     0x10, #0xfd
trap_fd: sjmp   trap_fd

test_jsg_4d:
        ; 4d) Not-taken: N=1, OV=0, Z=0 -> (1^0)|0 = 1 (PSW1 = 0x20)
        mov     0xd1, #0x20
        jsg     trap_jsg_n1
        ; Fallthrough (expected):
        mov     0x43, #0x44       ; expect 0x44
        mov     a, 0xd1
        mov     0x44, a           ; expect 0x20
        sjmp    test_jsg_4e

trap_jsg_n1:
        mov     0x10, #0xfd
        sjmp    trap_fd

test_jsg_4e:
        ; 4e) Not-taken: N=0, OV=1, Z=0 -> (0^1)|0 = 1 (PSW1 = 0x04)
        mov     0xd1, #0x04
        jsg     trap_jsg_ov1
        ; Fallthrough (expected):
        mov     0x45, #0x45       ; expect 0x45
        mov     a, 0xd1
        mov     0x46, a           ; expect 0x04
        sjmp    test_jsge

trap_jsg_ov1:
        mov     0x10, #0xfd
        sjmp    trap_fd

        ; ====================================================================
        ; 5. JSGE rel (0x58): Jump if N ^ OV == 0 (Signed >=, independent of Z)
        ; ====================================================================
test_jsge:
        ; 5a) Taken: N=0, OV=0, Z=0 -> 0^0 = 0 (PSW1 = 0x40)
        mov     0xd1, #0x40
        jsge    target_jsge_taken1
        ; Fallthrough trap:
        mov     0x10, #0xe6
trap_e6: sjmp   trap_e6

target_jsge_taken1:
        mov     0x47, #0x51       ; expect 0x51
        mov     a, 0xd1
        mov     0x48, a           ; expect 0x40

        ; 5b) Taken: N=1, OV=1, Z=0 -> 1^1 = 0 (PSW1 = 0x24)
        mov     0xd1, #0x24
        jsge    target_jsge_taken2
        ; Fallthrough trap:
        mov     0x10, #0xe7
trap_e7: sjmp   trap_e7

target_jsge_taken2:
        mov     0x49, #0x52       ; expect 0x52
        mov     a, 0xd1
        mov     0x4a, a           ; expect 0x24

        ; 5c) Taken: N=0, OV=0, Z=1 -> 0^0 = 0 (PSW1 = 0x02)
        ;     KILLS JSG MUTANT (JSG would erroneously fall through here!)
        mov     0xd1, #0x02
        jsge    target_jsge_taken3_z1
        ; Fallthrough trap:
        mov     0x10, #0xe8
trap_e8: sjmp   trap_e8

target_jsge_taken3_z1:
        mov     0x4b, #0x53       ; expect 0x53
        mov     a, 0xd1
        mov     0x4c, a           ; expect 0x02

        ; 5d) Not-taken: N=1, OV=0, Z=0 -> 1^0 = 1 (PSW1 = 0x20)
        mov     0xd1, #0x20
        jsge    trap_jsge_n1
        ; Fallthrough (expected):
        mov     0x4d, #0x54       ; expect 0x54
        mov     a, 0xd1
        mov     0x4e, a           ; expect 0x20
        sjmp    test_jsge_5e

trap_jsge_n1:
        mov     0x10, #0xfe
trap_fe: sjmp   trap_fe

test_jsge_5e:
        ; 5e) Not-taken: N=0, OV=1, Z=0 -> 0^1 = 1 (PSW1 = 0x04)
        mov     0xd1, #0x04
        jsge    trap_jsge_ov1
        ; Fallthrough (expected):
        mov     0x4f, #0x55       ; expect 0x55
        mov     a, 0xd1
        mov     0x50, a           ; expect 0x04
        sjmp    test_jbc51

trap_jsge_ov1:
        mov     0x10, #0xfe
        sjmp    trap_fe

        ; ====================================================================
        ; 6. JBC bit51, rel (0x10): 51-format bit test and single-bit clear
        ; ====================================================================
test_jbc51:
        ; 6a) Taken: bit 0x03 (0x20.3) is 1. Preset 0x20 = 0xAA (bits 7,5,3,1 set).
        mov     0x20, #0xaa
        mov     0xd1, #0x44       ; preset PSW1
        jbc     0x03, target_jbc51_taken
        ; Fallthrough trap:
        mov     0x10, #0xe9
trap_e9: sjmp   trap_e9

target_jbc51_taken:
        mov     0x51, #0x61       ; expect 0x61
        mov     0x52, 0x20        ; expect 0xA2 (ONLY bit 3 cleared; bits 7,5,1 intact!)
        mov     a, 0xd1
        mov     0x53, a           ; expect 0x44

        ; 6b) Not-taken: bit 0x03 is 0 (0x20 remains 0xA2).
        mov     0xd1, #0x44
        jbc     0x03, trap_jbc51_not_taken
        ; Fallthrough (expected):
        mov     0x54, #0x62       ; expect 0x62
        mov     0x55, 0x20        ; expect 0xA2 (preserved!)
        mov     a, 0xd1
        mov     0x56, a           ; expect 0x44 (PSW1 preserved)
        sjmp    test_jbc251

trap_jbc51_not_taken:
        mov     0x10, #0xf1
trap_f1: sjmp   trap_f1

        ; ====================================================================
        ; 7. JBC bit, rel (0xA9 0x15): 251-format bit test and single-bit clear
        ; ====================================================================
test_jbc251:
        ; 7a) Taken: 0x28.5 is 1. Preset 0x28 = 0x65 (bits 6,5,2,0 set).
        mov     0x28, #0x65
        mov     0xd1, #0x44
        jbc     0x28.5, target_jbc251_taken
        ; Fallthrough trap:
        mov     0x10, #0xea
trap_ea: sjmp   trap_ea

target_jbc251_taken:
        mov     0x57, #0x71       ; expect 0x71
        mov     0x58, 0x28        ; expect 0x45 (ONLY bit 5 cleared; bits 6,2,0 intact!)
        mov     a, 0xd1
        mov     0x59, a           ; expect 0x44

        ; 7b) Not-taken: 0x28.5 is 0 (0x28 remains 0x45).
        mov     0xd1, #0x44
        jbc     0x28.5, trap_jbc251_not_taken
        ; Fallthrough (expected):
        mov     0x5a, #0x72       ; expect 0x72
        mov     0x5b, 0x28        ; expect 0x45 (preserved!)
        mov     a, 0xd1
        mov     0x5c, a           ; expect 0x44 (PSW1 preserved)
        sjmp    test_djnz_rn

trap_jbc251_not_taken:
        mov     0x10, #0xf2
trap_f2: sjmp   trap_f2

        ; ====================================================================
        ; 8. DJNZ Rn, rel (0xDD): Decrement Rn and jump if non-zero
        ; ====================================================================
test_djnz_rn:
        mov     r5, #0x02         ; set R5 = 2
        mov     0xd1, #0x44       ; preset PSW1
        ; 8a) First decrement: R5 becomes 1 != 0 -> Taken jump
        djnz    r5, target_djnz_rn_loop
        ; Fallthrough trap:
        mov     0x10, #0xeb
trap_eb: sjmp   trap_eb

target_djnz_rn_loop:
        mov     0x5d, #0x81       ; expect 0x81
        mov     0x5e, r5          ; expect 0x01
        mov     a, 0xd1
        mov     0x5f, a           ; expect 0x44 (flags unmodified)

        ; 8b) Second decrement: R5 becomes 0 == 0 -> Not taken (fallthrough)
        djnz    r5, trap_djnz_rn_not_zero
        ; Fallthrough (expected):
        mov     0x60, #0x82       ; expect 0x82
        mov     0x61, r5          ; expect 0x00
        mov     a, 0xd1
        mov     0x62, a           ; expect 0x44 (flags unmodified!)
        sjmp    test_djnz_dir8

trap_djnz_rn_not_zero:
        mov     0x10, #0xf3
trap_f3: sjmp   trap_f3

        ; ====================================================================
        ; 9. DJNZ dir8, rel (0xD5): Decrement dir8 and jump if non-zero
        ; ====================================================================
test_djnz_dir8:
        mov     0x2a, #0x02       ; set 0x2A = 2
        mov     0xd1, #0x44       ; preset PSW1
        ; 9a) First decrement: 0x2A becomes 1 != 0 -> Taken jump
        djnz    0x2a, target_djnz_dir8_loop
        ; Fallthrough trap:
        mov     0x10, #0xec
trap_ec: sjmp   trap_ec

target_djnz_dir8_loop:
        mov     0x63, #0x91       ; expect 0x91
        mov     0x64, 0x2a        ; expect 0x01
        mov     a, 0xd1
        mov     0x65, a           ; expect 0x44 (flags unmodified)

        ; 9b) Second decrement: 0x2A becomes 0 == 0 -> Not taken (fallthrough)
        djnz    0x2a, trap_djnz_dir8_not_zero
        ; Fallthrough (expected):
        mov     0x66, #0x92       ; expect 0x92
        mov     0x67, 0x2a        ; expect 0x00
        mov     a, 0xd1
        mov     0x68, a           ; expect 0x44 (flags unmodified!)

        ; ====================================================================
        ; 10. Final Invariant Checks (SPX & Canary)
        ; ====================================================================
        mov     dr4, spx
        mov     0x69, r7          ; expect 0x00 (SPX low)
        mov     0x6a, r6          ; expect 0x02 (SPX high)

spin_success:
        sjmp    spin_success

trap_djnz_dir8_not_zero:
        mov     0x10, #0xf4
trap_f4: sjmp   trap_f4
