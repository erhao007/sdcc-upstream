; MCS-251 ISA semantics v5 — indexed MOV effective-address computation,
; all eight A5 forms.  Register naming is byte-indexed: wr4 = R4:R5,
; dr16 = R16..R19, wr6 = R6:R7 (nibble fields divide by operand width).
;
; DRk-base family (24-bit EA = DRk + dis, targets in the XDATA bank
; 0x010000+, unreachable by any 16-bit EA): 0x29 byte load, 0x69 word
; load, 0x39 byte store, 0x79 word store.  Read-backs use the
; NON-indexed @DRk forms — an independent path that cannot mask a
; truncation in the forms under test.  The boundary case (base
; 0x00FFFE + 2) rejects fixes that keep only the base's high byte.
;
; WRj-base family (16-bit EA = WRj + dis, targets in the edata window
; 0x0100-0xFFFF): 0x09 byte load, 0x49 word load, 0x19 byte store,
; 0x59 word store.  Read-backs again go through non-indexed @DRk
; forms with a 24-bit DR pointing at the same flat address, so a
; wrong-base execution (e.g. 0x59 using DR(idx*4)) cannot round-trip.
;
; ALL store targets — correct addresses AND their wrong aliases
; (truncated low-window / non-wrapped XDATA) — are PRESET via
; independent paths (@DRk or direct dir8) so every read-back and every
; wrong-address mutant is deterministic (never uCsim's random initial
; memory), and three wrap cases pin the 16-bit address space:
; 0x09 with base 0xfff0 + dis 0x0060 must land at 0x0050 (alias
; 0x010050 preset to a different value); the 0x59 word tail (first
; byte at 0xffff) must wrap its second byte to 0x0000 — iram[0x00]
; therefore ends at the wrapped store's low byte (0x77), a positive
; wrap assertion; and a 0x59 base-addition overflow (0xff10 + 0x00ff)
; must land the whole word at 0x000f/0x0010 (aliases preset).  iram
; [0x02] stays a wrong-base canary.  Results land in IRAM 0x30..0x3f
; and 0x41..0x42.  The displacement values are distinct (0x50-0x54
; for the plain WR cases) so the runner also asserts the full
; disassembly text of each form.
        .module isasem5
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        mov     spx, #0x0200      ; 251 stack lives at SPX, not legacy SP
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
        mov     r0, #0x00         ; canary: wrong-base stores land at iram[0]
        mov     r2, #0xaa         ; canary: never legitimately written again

        mov     dr16, #0x0000
        movh    dr16, #0x0001     ; DR16 = 0x010000 (XDATA bank)
        mov     dr20, #0x0000
        movh    dr20, #0x0001     ; DR20 = 0x010000 (read-back base)

        ; === 0x29: MOV Rm,@DRk+dis24 — byte load ===
        mov     r3, #0x5a
        mov     @dr16, r3         ; independent preset: [0x010000] = 0x5a
        mov     r10, @dr16+0x0000 ; under test
        mov     0x30, r10         ; expect 0x5a

        ; === 0x69: MOV WRj,@DRk+dis24 — word load ===
        mov     wr4, #0xabcd      ; wr4 = R4:R5
        mov     @dr16, wr4        ; independent preset: [0x010000]=AB [0x010001]=CD
        mov     wr6, @dr16+0x0000 ; under test (wr6 = R6:R7)
        mov     0x31, r6          ; expect 0xab
        mov     0x32, r7          ; expect 0xcd

        ; === 0x39: MOV @DRk+dis24,Rm — byte store ===
        inc     dr20, #4          ; DR20 = 0x010004
        mov     r3, #0x00
        mov     @dr20, r3         ; preset correct target [0x010004] = 0
        mov     0x04, #0x00       ; preset truncated alias iram[0x04] = 0
        mov     r3, #0x3c
        mov     @dr16+0x0004, r3  ; under test: writes [0x010004]
        mov     r13, @dr20        ; independent read-back
        mov     0x33, r13         ; expect 0x3c (truncation leaves 0x00)

        ; === 0x79: MOV @DRk+dis24,WRj — word store ===
        inc     dr20, #4          ; DR20 = 0x010008
        mov     r3, #0x00
        mov     @dr20, r3         ; preset correct target [0x010008] = 0
        inc     dr20, #1          ; DR20 = 0x010009
        mov     @dr20, r3         ; preset correct target [0x010009] = 0
        mov     0x08, #0x00       ; preset truncated aliases iram[0x08..09]
        mov     0x09, #0x00
        mov     wr4, #0xbeef
        mov     @dr16+0x0008, wr4 ; under test: writes [0x010008]=BE [0x010009]=EF
        dec     dr20, #1          ; DR20 = 0x010008
        mov     r14, @dr20        ; independent read-back (hi)
        inc     dr20, #1          ; DR20 = 0x010009
        mov     r15, @dr20        ; independent read-back (lo)
        mov     0x34, r14         ; expect 0xbe (truncation leaves 0x00)
        mov     0x35, r15         ; expect 0xef

        ; === 0x29 across the 64 KiB boundary: true 24-bit add ===
        mov     r3, #0x77
        mov     @dr16, r3         ; [0x010000] = 0x77
        dec     dr16, #2          ; DR16 = 0x00FFFE
        mov     r10, @dr16+0x0002 ; under test: EA must carry into bits 23:16
        mov     0x36, r10         ; expect 0x77

        ; === WRj-base family: EA = WRj + dis in the 16-bit space ===
        ; targets live at flat 0x0300+: beyond the program's code
        ; footprint (image ends near 0x01a9), so the read_edata ROM
        ; mirror cannot shadow the xram presets (the mirror returns
        ; code bytes for any ROM-loaded address)
        mov     dr24, #0x0300     ; read-back base, flat 0x000300

        ; === 0x09: MOV Rm,@WRj+dis16 — byte load ===
        mov     r3, #0x31
        mov     @dr24, r3         ; independent preset: [0x0300] = 0x31
        mov     wr6, #0x02b0      ; WR6 base
        mov     r10, @wr6+0x0050  ; under test: EA = 0x0300
        mov     0x37, r10         ; expect 0x31

        ; === 0x49: MOV WRj,@WRj+dis16 — word load ===
        inc     dr24, #1          ; DR24 = 0x0301
        mov     r3, #0x55
        mov     @dr24, r3         ; [0x0301] = 0x55
        inc     dr24, #1          ; DR24 = 0x0302
        mov     r3, #0x66
        mov     @dr24, r3         ; [0x0302] = 0x66
        mov     wr6, #0x02b0
        mov     wr6, @wr6+0x0051  ; under test: EA = 0x0301 (wr6 = R6:R7)
        mov     0x38, r6          ; expect 0x55
        mov     0x39, r7          ; expect 0x66

        ; === 0x19: MOV @WRj+dis16,Rm — byte store ===
        inc     dr24, #1          ; DR24 = 0x0303
        mov     r3, #0x00
        mov     @dr24, r3         ; preset [0x0303] = 0 (deterministic oracle)
        mov     r3, #0x77
        mov     wr6, #0x02b0
        mov     @wr6+0x0053, r3   ; under test: writes [0x0303]
        mov     r10, @dr24        ; independent read-back
        mov     0x3a, r10         ; expect 0x77

        ; === 0x59: MOV @WRj+dis16,WRj — word store ===
        inc     dr24, #1          ; DR24 = 0x0304
        mov     r3, #0x00
        mov     @dr24, r3         ; preset [0x0304] = 0
        inc     dr24, #1          ; DR24 = 0x0305
        mov     @dr24, r3         ; preset [0x0305] = 0
        mov     wr4, #0x8899
        mov     wr6, #0x02b0
        mov     @wr6+0x0054, wr4  ; under test: writes [0x0304]=88 [0x0305]=99
        dec     dr24, #1          ; DR24 = 0x0304
        mov     r10, @dr24        ; independent read-back (hi)
        mov     0x3b, r10         ; expect 0x88
        inc     dr24, #1          ; DR24 = 0x0305
        mov     r10, @dr24        ; independent read-back (lo)
        mov     0x3c, r10         ; expect 0x99

        ; === 0x09 base+dis wrap: 0xfff0 + 0x0060 -> 0x0050 ===
        mov     0x50, #0x93       ; preset correct target (direct path)
        mov     dr24, #0x0050
        movh    dr24, #0x0001     ; DR24 = 0x010050 (the non-wrapped alias)
        mov     r3, #0x77
        mov     @dr24, r3         ; preset alias [0x010050] = 0x77 != 0x93
        mov     wr6, #0xfff0
        mov     r10, @wr6+0x0060  ; under test: EA = 0x10050 must wrap to 0x0050
        mov     0x3d, r10         ; expect 0x93 (no-mask reads alias 0x77)

        ; === 0x59 word-tail wrap: hi at 0xffff, lo wraps to 0x0000 ===
        mov     dr24, #0x0000
        movh    dr24, #0x0001
        dec     dr24, #1          ; DR24 = 0x0000ffff
        mov     r3, #0x00
        mov     @dr24, r3         ; preset [0xffff] = 0 (deterministic oracle)
        mov     wr4, #0xcc77
        mov     wr6, #0xff00
        mov     @wr6+0x00ff, wr4  ; under test: hi -> [0xffff], lo -> [0x0000]
        mov     r10, @dr24        ; independent read-back (hi at 0xffff)
        mov     0x3e, r10         ; expect 0xcc
        mov     dr24, #0x0000     ; flat 0x000000 == iram[0]
        mov     r10, @dr24        ; independent read-back (wrapped lo)
        mov     0x3f, r10         ; expect 0x77

        ; === 0x59 base+dis overflow: 0xff10 + 0x00ff -> 0x000f ===
        ; (the tail-wrap case above keeps WR+dis == 0xffff; this one
        ;  overflows the ADDITION itself, so a mutant that only masks
        ;  the second byte still fails)
        mov     0x0f, #0x00       ; preset correct target hi (bank-1 R7 cell)
        mov     0x10, #0x00       ; preset correct target lo
        mov     dr24, #0x000f
        movh    dr24, #0x0001     ; DR24 = 0x01000f (non-wrapped alias)
        mov     r3, #0x66
        mov     @dr24, r3         ; preset alias [0x01000f] = 0x66
        inc     dr24, #1
        mov     @dr24, r3         ; preset alias [0x010010] = 0x66
        mov     wr4, #0xaabb
        mov     wr6, #0xff10
        mov     @wr6+0x00ff, wr4  ; under test: hi -> [0x000f], lo -> [0x0010]
        mov     dr24, #0x000f     ; DR24 = 0x00000f (flat iram[0x0f];
        mov     r10, @dr24        ;  NB iram[0x0f] is bank-1 R7, NOT R15 —
        mov     0x41, r10         ;  R8-R31 live in the separate register file)
        inc     dr24, #1          ; DR24 = 0x000010 (flat iram[0x10])
        mov     r10, @dr24        ; independent read-back (lo)
        mov     0x42, r10         ; expect 0xbb

spin:   sjmp    spin
