; MCS-251 ISA semantics v8 — ST-1S-C: Call, Return & Unconditional Jump.
; Independent oracle from public manuals:
;   - Intel 8XC251SB User's Manual, doc 272617-001, February 1995:
;       LCALL addr16 / LCALL @WRj: pp.A-77..A-78; flags: none
;       ECALL addr24 / ECALL @DRk: pp.A-50..A-51; flags: none
;       RET:                       p.A-128; flags: none
;       ERET:                      p.A-53;  flags: none
;       RETI:                      p.A-129; flags: restored from frame
;       AJMP addr11:               p.A-31;  flags: none
;       EJMP addr24 / EJMP @DRk:   pp.A-52..A-53; flags: none
;   - STC32G Microcontroller User's Manual (2022), doc STC32G-CN:
;       Instruction details: pp.814..815 (calls/jumps/returns);
;       CONFIG1.INTR=1 4-byte interrupt frame: p.158 / p.1701.
;
; Hardened Anti-Mutant & Multi-Region Assertions:
;   1) Anti-Fallthrough Traps: Every jump/return instruction is followed by
;      a dedicated trap "mov 0x30, #0xEx; sjmp ." so replacing jump with NOP
;      immediately halts with non-zero fail canary in IRAM[0x30].
;   2) 24-bit Multi-Region Targets: ECALL, EJMP, ERET and RETI targets live
;      in Region 1 (0x01xxxx, PC.23:16 == 0x01) to distinguish true 24-bit
;      handling from 16-bit truncation.
;   3) 64 KiB Region Preservation: LCALL addr16 and RET execute inside Region 1
;      and prove that PC.23:16 == 0x01 is preserved on near call & return.
;   4) Non-Zero 2 KiB Page Preservation: Base is 0x010900 (PC.15:11 = 0b00001 != 0),
;      proving AJMP preserves both PC.23:16 and non-zero page bits PC.15:11.
;   5) Unmasked Push/Pop Order: Callee inspects exact return-frame byte order
;      pushed on stack (little-endian: low byte at stack bottom SPX-1, high at
;      top SPX; 3-byte ECALL: low at bottom, middle, high at top).
;   6) Standalone Return Frames: RET and ERET execute against independently
;      synthesized stack frames without preceding CALLs to avoid mutual masking.
;   7) Full SPX (0x0200) & PSW1 (0xC4) verification across all operations.

        .module isasem8
        .area   MCS251CODE (ABS)
        .org    0x0000
start:
        mov     sp, #0x7f
        mov     spx, #0x0200

        ; === IRAM clean-up (0x30..0x7f) ===
        mov     r0, #0x30
clean_iram_loop:
        mov     @r0, #0x00
        inc     r0
        cjne    r0, #0x80, clean_iram_loop

        ; Stack boundary canary: xram[0x0200] = 0x55
        mov     dptr, #0x0200
        mov     a, #0x55
        movx    @dptr, a

        ; ====================================================================
        ; 1. Region 0: LCALL addr16 (0x12) & RET (0x22)
        ; ====================================================================
        mov     0xd1, #0xc4       ; preset PSW1
        lcall   sub_lcall16_r0
ret_from_lcall16_r0:
        mov     a, 0xd1
        mov     0x36, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x37, r7          ; expect 0x00 (SPX low)
        mov     0x38, r6          ; expect 0x02 (SPX high)

        ; ====================================================================
        ; 2. Region 0: LCALL @WRj (0x99 0x64) & RET (0x22)
        ; ====================================================================
        mov     wr6, #sub_lcall_wr_r0
        mov     0xd1, #0xc4       ; preset PSW1
        lcall   @wr6
ret_from_lcall_wr_r0:
        mov     a, 0xd1
        mov     0x3e, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x3f, r7          ; expect 0x00
        mov     0x40, r6          ; expect 0x02

        ; ====================================================================
        ; 3. ECALL addr24 (0x9A) to Region 1 (0x0109xx) & ERET (0xAA)
        ; ====================================================================
        mov     0xd1, #0xc4       ; preset PSW1
        ecall   sub_ecall24_r1
ret_from_ecall24_r1:
        mov     a, 0xd1
        mov     0x47, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x48, r7          ; expect 0x00
        mov     0x49, r6          ; expect 0x02

        ; ====================================================================
        ; 4. ECALL @DRk (0x99 0x78) to Region 1 (0x0109xx) & ERET (0xAA)
        ; ====================================================================
        mov     dr28, #sub_ecall_dr_r1
        movh    dr28, #0x0001     ; DR28 = 0x01xxxx (Region 1)
        mov     0xd1, #0xc4       ; preset PSW1
        ecall   @dr28
ret_from_ecall_dr_r1:
        mov     a, 0xd1
        mov     0x50, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x51, r7          ; expect 0x00
        mov     0x52, r6          ; expect 0x02

        ; ====================================================================
        ; 5. EJMP addr24 (0x8A) to Region 1 Main Stage
        ; ====================================================================
        mov     0xd1, #0xc4       ; preset PSW1
        ejmp    region1_main
        ; Fallthrough trap:
        mov     0x30, #0xe1
trap_e1: sjmp   trap_e1

        ; Landing target from Region 1 stages:
target_eret_r0:
        mov     0x67, #0x44       ; expect 0x44
        mov     a, 0xd1
        mov     0x68, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x69, r7          ; expect 0x00
        mov     0x6a, r6          ; expect 0x02

        ; ====================================================================
        ; 10. EJMP @DRk (0x89 0x78) to Region 1
        ; ====================================================================
        mov     dr28, #target_ejmp_dr_r1
        movh    dr28, #0x0001     ; DR28 = 0x01xxxx (Region 1)
        mov     0xd1, #0xc4       ; preset PSW1
        ejmp    @dr28
        ; Fallthrough trap:
        mov     0x30, #0xe5
trap_e5: sjmp   trap_e5

        ; Landing target from RETI in Region 1:
target_reti_r0:
        mov     0x6f, #0x66       ; expect 0x66
        mov     a, 0xd1
        mov     0x70, a           ; expect 0xE4 (restored PSW1 from frame)
        mov     dr4, spx
        mov     0x71, r7          ; expect 0x00
        mov     0x72, r6          ; expect 0x02

spin_success:
        sjmp    spin_success

; --- Region 0 Callee Subroutines ---
sub_lcall16_r0:
        mov     a, 0xd1
        mov     0x31, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x32, r7          ; expect 0x02 (SPX low)
        mov     0x33, r6          ; expect 0x02 (SPX high)
        mov     dptr, #0x0201
        movx    a, @dptr
        mov     0x34, a           ; expect ret_from_lcall16_r0 low byte
        mov     dptr, #0x0202
        movx    a, @dptr
        mov     0x35, a           ; expect ret_from_lcall16_r0 high byte
        ret

sub_lcall_wr_r0:
        mov     a, 0xd1
        mov     0x39, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x3a, r7          ; expect 0x02
        mov     0x3b, r6          ; expect 0x02
        mov     dptr, #0x0201
        movx    a, @dptr
        mov     0x3c, a           ; expect ret_from_lcall_wr_r0 low byte
        mov     dptr, #0x0202
        movx    a, @dptr
        mov     0x3d, a           ; expect ret_from_lcall_wr_r0 high byte
        ret

; ============================================================================
; REGION 1 CODE (Base 0x010900) — Non-zero 2 KiB page (PC.15:11 != 0) & Region
; ============================================================================
        .area   MCS251REG1 (CODE)
sub_ecall24_r1:
        mov     a, 0xd1
        mov     0x41, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x42, r7          ; expect 0x03 (SPX low)
        mov     0x43, r6          ; expect 0x02 (SPX high)
        mov     dptr, #0x0201
        movx    a, @dptr
        mov     0x44, a           ; expect ret_from_ecall24_r1 low byte
        mov     dptr, #0x0202
        movx    a, @dptr
        mov     0x45, a           ; expect ret_from_ecall24_r1 mid byte
        mov     dptr, #0x0203
        movx    a, @dptr
        mov     0x46, a           ; expect 0x00 (Region 0 high byte)
        eret

sub_ecall_dr_r1:
        mov     a, 0xd1
        mov     0x4a, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x4b, r7          ; expect 0x03
        mov     0x4c, r6          ; expect 0x02
        mov     dptr, #0x0201
        movx    a, @dptr
        mov     0x4d, a           ; expect ret_from_ecall_dr_r1 low byte
        mov     dptr, #0x0202
        movx    a, @dptr
        mov     0x4e, a           ; expect ret_from_ecall_dr_r1 mid byte
        mov     dptr, #0x0203
        movx    a, @dptr
        mov     0x4f, a           ; expect 0x00 (Region 0 high byte)
        eret

region1_main:
        mov     0x53, #0x11       ; expect 0x11
        mov     a, 0xd1
        mov     0x54, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x55, r7          ; expect 0x00
        mov     0x56, r6          ; expect 0x02

        ; ====================================================================
        ; 6. In Region 1: LCALL addr16 & RET (Region Preservation PC.23:16)
        ; ====================================================================
        mov     0xd1, #0xc4       ; preset PSW1
        lcall   sub_lcall16_r1_local
ret_from_lcall16_r1:
        mov     a, 0xd1
        mov     0x5c, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x5d, r7          ; expect 0x00
        mov     0x5e, r6          ; expect 0x02

        ; ====================================================================
        ; 7. In Region 1: Standalone Synthesized RET (0x22)
        ; ====================================================================
        mov     dptr, #0x0201
        mov     a, #target_ret_r1_standalone
        movx    @dptr, a
        mov     dptr, #0x0202
        mov     a, #target_ret_r1_standalone >> 8
        movx    @dptr, a
        mov     spx, #0x0202      ; frame top
        mov     0xd1, #0xc4       ; preset PSW1
        ret
        ; Fallthrough trap:
        mov     0x30, #0xe2
trap_e2: sjmp   trap_e2

target_ret_r1_standalone:
        mov     0x5f, #0x22       ; expect 0x22
        mov     a, 0xd1
        mov     0x60, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x61, r7          ; expect 0x00
        mov     0x62, r6          ; expect 0x02

        ; ====================================================================
        ; 8. In Region 1: AJMP addr11 (0xC1) with non-zero 2KB page bits
        ; ====================================================================
        mov     0xd1, #0xc4       ; preset PSW1
        ajmp    target_ajmp_r1
        ; Fallthrough trap:
        mov     0x30, #0xe3
trap_e3: sjmp   trap_e3

target_ajmp_r1:
        mov     0x63, #0x33       ; expect 0x33
        mov     a, 0xd1
        mov     0x64, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x65, r7          ; expect 0x00
        mov     0x66, r6          ; expect 0x02

        ; ====================================================================
        ; 9. Standalone Synthesized ERET (0xAA) from Region 1 to Region 0
        ; ====================================================================
        mov     dptr, #0x0201
        mov     a, #target_eret_r0
        movx    @dptr, a
        mov     dptr, #0x0202
        mov     a, #target_eret_r0 >> 8
        movx    @dptr, a
        mov     dptr, #0x0203
        mov     a, #0x00          ; Target in Region 0
        movx    @dptr, a
        mov     spx, #0x0203      ; frame top
        mov     0xd1, #0xc4       ; preset PSW1
        eret
        ; Fallthrough trap:
        mov     0x30, #0xe4
trap_e4: sjmp   trap_e4

sub_lcall16_r1_local:
        mov     a, 0xd1
        mov     0x57, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x58, r7          ; expect 0x02 (SPX low)
        mov     0x59, r6          ; expect 0x02 (SPX high)
        mov     dptr, #0x0201
        movx    a, @dptr
        mov     0x5a, a           ; expect ret_from_lcall16_r1 low byte
        mov     dptr, #0x0202
        movx    a, @dptr
        mov     0x5b, a           ; expect ret_from_lcall16_r1 high byte
        ret

target_ejmp_dr_r1:
        mov     0x6b, #0x55       ; expect 0x55
        mov     a, 0xd1
        mov     0x6c, a           ; expect 0xC4
        mov     dr4, spx
        mov     0x6d, r7          ; expect 0x00
        mov     0x6e, r6          ; expect 0x02

        ; ====================================================================
        ; 11. In Region 1: RETI (0x32) with 4-byte frame returning to Region 0
        ; ====================================================================
        mov     dptr, #0x0201
        mov     a, #0xe4          ; Frame PSW1
        movx    @dptr, a
        mov     dptr, #0x0202
        mov     a, #0x00          ; PC.23:16 (Region 0)
        movx    @dptr, a
        mov     dptr, #0x0203
        mov     a, #target_reti_r0
        movx    @dptr, a
        mov     dptr, #0x0204
        mov     a, #target_reti_r0 >> 8
        movx    @dptr, a
        mov     spx, #0x0204      ; set SPX to frame top
        mov     0xd1, #0x00       ; clear PSW1 before RETI
        reti
        ; Fallthrough trap:
        mov     0x30, #0xe6
trap_e6: sjmp   trap_e6
