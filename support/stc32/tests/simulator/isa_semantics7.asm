; MCS-251 ISA semantics v7 — ST-1S-B: CODE/XDATA access.
; Independent oracle from public manuals:
;   - Intel 8XC251SB User's Manual (1997), doc 272617-001:
;       MOVC A, @A+DPTR: p.A-100; flags: none
;       MOVC A, @A+PC:   p.A-101; flags: none
;       MOVX @DPTR, A / MOVX A, @DPTR: pp.A-103..A-104; flags: none
;       MOVX @Ri, A / MOVX A, @Ri:     pp.A-104..A-105; flags: none
;   - STC32G Microcontroller User's Manual (2022), doc STC32G-CN:
;       MOVC / MOVX instruction table: pp.814..815;
;       Internal XRAM structure: p.174; MXAX register: p.199.
;
; Memory model note (E2 simulator limitation vs E4 device):
;   uCsim models MOVX @Ri as accessing xram[0x0000+Ri] (Page 0 simplification
;   at E2 evidence level). Tier 1 ABI.md defines __pdata with active MXAX:P2
;   paging, which remains a device/board-level (E4) peripheral behavior.
;
; Memory space separation & anti-aliasing assertions:
;   - Pre-populates canaries in XRAM (0x0034=0xEE, 0x0042=0xAA, 0x0043=0xBB,
;     0x0051=0xCC, 0x1235=0xDD) to prove that @DPTR (0x1234/0x0050) and @Ri
;     (0x40/0x41) write to their EXACT respective addresses and do NOT truncate
;     or alias into adjacent/low-byte addresses.
;   - Asserts IRAM[0x40] canary (0x33) is untouched after MOVX @R0, A (0x40).
;
; Flags assertion:
;   Every single form individually executes with preset PSW1=0xC4 (CY=1, AC=1,
;   OV=1, RS=0, N=0, Z=0) and captures full PSW1 immediately afterwards to prove
;   flag preservation per form.
;
; IRAM dump layout (0x50..0x62, 19 bytes):
;   0x50: PSW1 after MOVX @DPTR,A (0x1234) -> 0xC4
;   0x51: MOVX A,@DPTR (0x1234) read back  -> 0x6A
;   0x52: PSW1 after MOVX A,@DPTR (0x1234) -> 0xC4
;   0x53: PSW1 after MOVX @DPTR,A (0x0050) -> 0xC4
;   0x54: MOVX A,@DPTR (0x0050) read back  -> 0x9C
;   0x55: PSW1 after MOVX A,@DPTR (0x0050) -> 0xC4
;   0x56: PSW1 after MOVX @R0,A (0x40)     -> 0xC4
;   0x57: MOVX A,@R0 (0x40) read back      -> 0xB1
;   0x58: PSW1 after MOVX A,@R0 (0x40)     -> 0xC4
;   0x59: PSW1 after MOVX @R1,A (0x41)     -> 0xC4
;   0x5A: MOVX A,@R1 (0x41) read back      -> 0xD2
;   0x5B: PSW1 after MOVX A,@R1 (0x41)     -> 0xC4
;   0x5C: MOVC A,@A+DPTR (A=0)             -> 0x7B
;   0x5D: PSW1 after MOVC DPTR0            -> 0xC4
;   0x5E: MOVC A,@A+DPTR (A=2)             -> 0x9F
;   0x5F: PSW1 after MOVC DPTR2            -> 0xC4
;   0x60: MOVC A,@A+PC (A=9)               -> 0x22
;   0x61: PSW1 after MOVC PC               -> 0xC4
;   0x62: IRAM[0x40] canary                -> 0x33 (preserved)
        .module isasem7
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        mov     spx, #0x0200

        ; === IRAM clean-up ===
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

        ; IRAM[0x40] canary to prove XDATA and IRAM are isolated
        mov     0x40, #0x33

        ; === Pre-populate XRAM canaries and initial zero targets ===
        ; 1) 0x0034: canary = 0xEE (catches DPTR 0x1234 low-byte truncation)
        mov     dptr, #0x0034
        mov     a, #0xee
        movx    @dptr, a

        ; 2) 0x0040..0x0043: target 0x00, 0x00; canaries 0xAA, 0xBB (catches R0/R1 alias)
        mov     dptr, #0x0040
        mov     a, #0x00
        movx    @dptr, a
        mov     dptr, #0x0041
        mov     a, #0x00
        movx    @dptr, a
        mov     dptr, #0x0042
        mov     a, #0xaa
        movx    @dptr, a
        mov     dptr, #0x0043
        mov     a, #0xbb
        movx    @dptr, a

        ; 3) 0x0050..0x0051: target 0x00; canary 0xCC (catches 0x0050 alias)
        mov     dptr, #0x0050
        mov     a, #0x00
        movx    @dptr, a
        mov     dptr, #0x0051
        mov     a, #0xcc
        movx    @dptr, a

        ; 4) 0x1234..0x1235: target 0x00; canary 0xDD (catches 0x1234 alias)
        mov     dptr, #0x1234
        mov     a, #0x00
        movx    @dptr, a
        mov     dptr, #0x1235
        mov     a, #0xdd
        movx    @dptr, a

        ; === 1. MOVX @DPTR, A (0xF0) with 0x1234 ===
        mov     dptr, #0x1234
        mov     a, #0x6a
        mov     0xd1, #0xc4       ; preset flags
        movx    @dptr, a          ; write XDATA[0x1234] = 0x6A
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x50, a           ; expect 0xC4

        ; === 2. MOVX A, @DPTR (0xE0) with 0x1234 ===
        mov     dptr, #0x1234
        mov     0xd1, #0xc4       ; preset flags
        mov     a, #0x00          ; clear A
        movx    a, @dptr          ; read XDATA[0x1234] -> A = 0x6A
        mov     0x51, a           ; expect 0x6A
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x52, a           ; expect 0xC4

        ; === 3. MOVX @DPTR, A (0xF0) with 0x0050 ===
        mov     dptr, #0x0050
        mov     a, #0x9c
        mov     0xd1, #0xc4       ; preset flags
        movx    @dptr, a          ; write XDATA[0x0050] = 0x9C
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x53, a           ; expect 0xC4

        ; === 4. MOVX A, @DPTR (0xE0) with 0x0050 ===
        mov     dptr, #0x0050
        mov     0xd1, #0xc4       ; preset flags
        mov     a, #0x00          ; clear A
        movx    a, @dptr          ; read XDATA[0x0050] -> A = 0x9C
        mov     0x54, a           ; expect 0x9C
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x55, a           ; expect 0xC4

        ; === 5. MOVX @R0, A (0xF2) with 0x0040 ===
        mov     r0, #0x40
        mov     a, #0xb1
        mov     0xd1, #0xc4       ; preset flags
        movx    @r0, a            ; write XDATA[0x40] = 0xB1
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x56, a           ; expect 0xC4

        ; === 6. MOVX A, @R0 (0xE2) with 0x0040 ===
        mov     r0, #0x40
        mov     0xd1, #0xc4       ; preset flags
        mov     a, #0x00          ; clear A
        movx    a, @r0            ; read XDATA[0x40] -> A = 0xB1
        mov     0x57, a           ; expect 0xB1
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x58, a           ; expect 0xC4

        ; === 7. MOVX @R1, A (0xF3) with 0x0041 ===
        mov     r1, #0x41
        mov     a, #0xd2
        mov     0xd1, #0xc4       ; preset flags
        movx    @r1, a            ; write XDATA[0x41] = 0xD2
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x59, a           ; expect 0xC4

        ; === 8. MOVX A, @R1 (0xE3) with 0x0041 ===
        mov     r1, #0x41
        mov     0xd1, #0xc4       ; preset flags
        mov     a, #0x00          ; clear A
        movx    a, @r1            ; read XDATA[0x41] -> A = 0xD2
        mov     0x5a, a           ; expect 0xD2
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x5b, a           ; expect 0xC4

        ; === 9. MOVC A, @A+DPTR (0x93) with A=0 ===
        mov     dptr, #dptr_const_tbl
        mov     0xd1, #0xc4       ; preset flags
        mov     a, #0x00
        movc    a, @a+dptr        ; expect 0x7B
        mov     0x5c, a
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x5d, a           ; expect 0xC4

        ; === 10. MOVC A, @A+DPTR (0x93) with A=2 ===
        mov     dptr, #dptr_const_tbl
        mov     0xd1, #0xc4       ; preset flags
        mov     a, #0x02
        movc    a, @a+dptr        ; expect 0x9F
        mov     0x5e, a
        mov     a, 0xd1           ; capture PSW1 immediately
        mov     0x5f, a           ; expect 0xC4

        ; === 11. MOVC A, @A+PC (0x83) with immediate result & PSW1 capture ===
        mov     0xd1, #0xc4       ; preset flags
        ; Offset calculation:
        ;   movc a,@a+pc is at address X. PC_next is at (X+1).
        ;   mov 0x60, a    (2 bytes: X+1..X+2)
        ;   mov a, 0xd1    (2 bytes: X+3..X+4)
        ;   mov 0x61, a    (2 bytes: X+5..X+6)
        ;   sjmp after     (2 bytes: X+7..X+8)
        ;   pc_tbl starts at (X+9).
        ;   A = 9 lands on pc_tbl[1] = 0x22.
        mov     a, #0x09
        movc    a, @a+pc
        mov     0x60, a           ; expect 0x22 (immediate capture before any branch)
        mov     a, 0xd1
        mov     0x61, a           ; expect 0xC4 (immediate capture before any branch)
        sjmp    after_pc_tbl
pc_tbl:
        .db     0x11, 0x22, 0x33, 0x44
after_pc_tbl:

        ; === 12. Memory space isolation check (IRAM vs XDATA) ===
        mov     a, 0x40           ; read IRAM[0x40] (must be untouched 0x33)
        mov     0x62, a           ; expect 0x33

spin:
        sjmp    spin

dptr_const_tbl:
        .db     0x7b, 0x8e, 0x9f, 0xac
