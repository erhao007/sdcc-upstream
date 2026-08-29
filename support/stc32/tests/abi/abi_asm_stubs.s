; abi_asm_stubs.s - Hand-written assembly stubs to strictly verify ABI register slots and contracts
; This file independently asserts the calling convention without relying on SDCC code generator.

        .module abi_asm_stubs
        .globl  _asm_callee_u8
        .globl  _asm_callee_u16
        .globl  _asm_callee_ptr
        .globl  _asm_callee_u32
        .globl  _asm_callee_u40
        .globl  _asm_callee_u48
        .globl  _asm_callee_u56
        .globl  _asm_callee_u64
        .globl  _asm_callee_bit_ret_set
        .globl  _asm_callee_bit_ret_clr
        .globl  _asm_callee_bit_param_nr
        .globl  _asm_callee_bit_param_nr_PARM_1
        .globl  _asm_callee_bit_param_reent
        .globl  _asm_caller_call_c_u32
        .globl  _c_callee_u32

        .area   BSEG (BIT)
        .globl  _asm_callee_bit_param_nr_PARM_1
_asm_callee_bit_param_nr_PARM_1:
        .ds     1

        .area BIT_BANK	(REL,OVR,DATA)
bits:
	.ds 1
	b0 = bits[0]
	b1 = bits[1]
	b2 = bits[2]
	b3 = bits[3]
	b4 = bits[4]
	b5 = bits[5]
	b6 = bits[6]
	b7 = bits[7]

        .area   CSEG (CODE)

; ====================================================================
; 1. 1-byte scalar: arg in DPL, return in DPL
; ====================================================================
_asm_callee_u8:
        mov     a, dpl
        inc     a
        mov     dpl, a
        eret

; ====================================================================
; 2. 2-byte scalar: arg in DPH:DPL, return in DPH:DPL
; ====================================================================
_asm_callee_u16:
        mov     a, dpl
        add     a, #0x02
        mov     dpl, a
        mov     a, dph
        addc    a, #0x01
        mov     dph, a
        eret

; ====================================================================
; 3. 3-byte pointer: arg in B:DPH:DPL, return in B:DPH:DPL
; ====================================================================
_asm_callee_ptr:
        mov     a, dpl
        add     a, #0x03
        mov     dpl, a
        mov     a, dph
        addc    a, #0x00
        mov     dph, a
        mov     a, b
        addc    a, #0x00
        mov     b, a
        eret

; ====================================================================
; 4. 4-byte scalar: arg in A:B:DPH:DPL, return in A:B:DPH:DPL
; ====================================================================
_asm_callee_u32:
        mov     r0, a             ; save A (byte 3)
        mov     a, dpl
        add     a, #0x01
        mov     dpl, a
        mov     a, dph
        addc    a, #0x00
        mov     dph, a
        mov     a, b
        addc    a, #0x00
        mov     b, a
        mov     a, r0
        addc    a, #0x01          ; byte 3 in A
        eret

; ====================================================================
; 5. 5-byte scalar (_BitInt(40)): arg in DPL, DPH, B, A, R4
; ====================================================================
_asm_callee_u40:
        mov     r0, a             ; save A (byte 3)
        mov     a, dpl
        add     a, #0x01
        mov     dpl, a
        mov     a, dph
        addc    a, #0x00
        mov     dph, a
        mov     a, b
        addc    a, #0x00
        mov     b, a
        mov     a, r0
        addc    a, #0x00
        mov     r0, a
        mov     a, r4
        addc    a, #0x05          ; byte 4 in R4 modified by +0x05
        mov     r4, a
        mov     a, r0
        eret

; ====================================================================
; 6. 6-byte scalar (_BitInt(48)): arg in DPL, DPH, B, A, R4, R5
; ====================================================================
_asm_callee_u48:
        mov     r0, a
        mov     a, dpl
        add     a, #0x01
        mov     dpl, a
        mov     a, dph
        addc    a, #0x00
        mov     dph, a
        mov     a, b
        addc    a, #0x00
        mov     b, a
        mov     a, r0
        addc    a, #0x00
        mov     r0, a
        mov     a, r4
        addc    a, #0x00
        mov     r4, a
        mov     a, r5
        addc    a, #0x06          ; byte 5 in R5 modified by +0x06
        mov     r5, a
        mov     a, r0
        eret

; ====================================================================
; 7. 7-byte scalar (_BitInt(56)): arg in DPL, DPH, B, A, R4, R5, R6
; ====================================================================
_asm_callee_u56:
        mov     r0, a
        mov     a, dpl
        add     a, #0x01
        mov     dpl, a
        mov     a, dph
        addc    a, #0x00
        mov     dph, a
        mov     a, b
        addc    a, #0x00
        mov     b, a
        mov     a, r0
        addc    a, #0x00
        mov     r0, a
        mov     a, r4
        addc    a, #0x00
        mov     r4, a
        mov     a, r5
        addc    a, #0x00
        mov     r5, a
        mov     a, r6
        addc    a, #0x07          ; byte 6 in R6 modified by +0x07
        mov     r6, a
        mov     a, r0
        eret

; ====================================================================
; 8. 8-byte scalar (uint64_t / _BitInt(64)): DPL, DPH, B, A, R4, R5, R6, R7
; ====================================================================
_asm_callee_u64:
        mov     r0, a
        mov     a, dpl
        add     a, #0x01
        mov     dpl, a
        mov     a, dph
        addc    a, #0x00
        mov     dph, a
        mov     a, b
        addc    a, #0x00
        mov     b, a
        mov     a, r0
        addc    a, #0x00
        mov     r0, a
        mov     a, r4
        addc    a, #0x00
        mov     r4, a
        mov     a, r5
        addc    a, #0x00
        mov     r5, a
        mov     a, r6
        addc    a, #0x00
        mov     r6, a
        mov     a, r7
        addc    a, #0x08          ; byte 7 in R7 modified by +0x08
        mov     r7, a
        mov     a, r0
        eret

; ====================================================================
; 9. Bit return via CY flag
; ====================================================================
_asm_callee_bit_ret_set:
        setb    c
        eret

_asm_callee_bit_ret_clr:
        clr     c
        eret

; ====================================================================
; 10. Bit parameter in Ordinary mode (via BSEG _PARM_1) & inverted return via CY
; ====================================================================
_asm_callee_bit_param_nr:
        mov     c, _asm_callee_bit_param_nr_PARM_1
        cpl     c
        eret

; ====================================================================
; 11. Bit parameter in Reentrant mode (via b0 register) & inverted return via CY
; ====================================================================
_asm_callee_bit_param_reent:
        mov     c, b0
        cpl     c
        eret

; ====================================================================
; 12. Hand-written Assembly Caller to C callee
; ====================================================================
_asm_caller_call_c_u32:
        mov     dpl, #0x44
        mov     dph, #0x33
        mov     b, #0x22
        mov     a, #0x11
        ecall   _c_callee_u32
        ; C callee returns A:B:DPH:DPL = 0x22334455
        mov     r0, a
        mov     a, dpl
        cjne    a, #0x55, fail_u32
        mov     a, dph
        cjne    a, #0x44, fail_u32
        mov     a, b
        cjne    a, #0x33, fail_u32
        mov     a, r0
        cjne    a, #0x22, fail_u32
        mov     dpl, #0x01
        eret
fail_u32:
        mov     dpl, #0x00
        eret
