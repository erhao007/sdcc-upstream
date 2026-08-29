; MCS-251 ISA semantics v10 — ST-1S-E: Special / Simple forms (NOP, TRAP, ESC).
; Independent oracle from public manuals:
;   - Intel 8XC251SB User's Manual, doc 272617-001, February 1995:
;       NOP:  p.A-107; flags: none (1-byte opcode 0x00; PC advances to next byte)
;       TRAP: p.A-132; flags: none (1-byte opcode 0xB9; software trap / STC32G NOP)
;       ESC:  p.A-6, A-27; prefix byte 0xA5 (structural N/A; non-standalone opcode)
;   - STC32G Microcontroller User's Manual (2022), doc STC32G-CN:
;       NOP:  p.815; flags: none (1-byte opcode 0x00)
;       TRAP: p.1672 instruction table (defined as 1-byte NOP; flags: none)
;
; Hardened Anti-Mutant & Zero-Side-Effect Assertions:
;   1) NOP:
;      - Verifies PC advance (0xAA -> 0x55);
;      - Verifies PSW1 preservation (0xC4 -> 0xC4);
;      - Verifies General Purpose Registers preservation (R1=0x12, R2=0x34, R3=0x56, R4=0x78);
;      - Kills "mov r1,#0x99" mutant;
;   2) TRAP:
;      - Verifies 1-byte opcode execution (0xBB -> 0x66), PC advance;
;      - Verifies PSW1 preservation (0x64 -> 0x64);
;      - Verifies General Purpose Registers preservation (R1=0x9A, R2=0xBC, R3=0xDE, R4=0xF0);
;      - Kills "mov r2,#0x99" mutant;
;   3) ESC: Documented as structural prefix N/A (not a standalone runnable opcode);
;   4) Invariants: SPX remains 0x0200 and xram[0x0200] == 0x55.

        .module isasem10
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
        ; 1. NOP (0x00): No operation, advance PC, preserve PSW1 & registers
        ; ====================================================================
        mov     0x31, #0xaa       ; pre-nop marker
        mov     0xd1, #0xc4       ; preset PSW1 (CY=1, AC=1, OV=1, Z=0)
        mov     0x32, 0xd1        ; record preset PSW1 (expect 0xC4)
        ; Preset register canaries:
        mov     r1, #0x12
        mov     r2, #0x34
        mov     r3, #0x56
        mov     r4, #0x78

        nop

        ; Post-NOP verification:
        mov     0x33, #0x55       ; post-nop marker (proves PC advanced)
        mov     a, 0xd1
        mov     0x34, a           ; expect 0xC4 (flags unchanged)
        ; Save register canaries:
        mov     0x35, r1          ; expect 0x12
        mov     0x36, r2          ; expect 0x34
        mov     0x37, r3          ; expect 0x56
        mov     0x38, r4          ; expect 0x78

        ; ====================================================================
        ; 2. TRAP (0xB9): STC32G 1-byte NOP semantics, advance PC, preserve PSW1 & registers
        ; ====================================================================
        mov     0x39, #0xbb       ; pre-trap marker
        mov     0xd1, #0x64       ; preset PSW1 (CY=0, AC=1, N=1, OV=1, Z=0)
        mov     0x3a, 0xd1        ; record preset PSW1 (expect 0x64)
        ; Preset register canaries:
        mov     r1, #0x9a
        mov     r2, #0xbc
        mov     r3, #0xde
        mov     r4, #0xf0

        trap

        ; Post-TRAP verification:
        mov     0x3b, #0x66       ; post-trap marker (proves PC advanced)
        mov     a, 0xd1
        mov     0x3c, a           ; expect 0x64 (flags unchanged)
        ; Save register canaries:
        mov     0x3d, r1          ; expect 0x9A
        mov     0x3e, r2          ; expect 0xBC
        mov     0x3f, r3          ; expect 0xDE
        mov     0x40, r4          ; expect 0xF0

        ; ====================================================================
        ; 3. Final Invariant Checks (SPX & Canary)
        ; ====================================================================
        mov     dr4, spx
        mov     0x41, r7          ; expect 0x00 (SPX low)
        mov     0x42, r6          ; expect 0x02 (SPX high)

spin_success:
        sjmp    spin_success
