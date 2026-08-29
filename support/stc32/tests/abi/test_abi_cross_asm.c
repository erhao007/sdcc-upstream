/* test_abi_cross_asm.c - Cross-language C/ASM boundary test locking exact 1-8 byte ABI slots and bit convention */
#include "abi_test.h"

extern uint8_t asm_callee_u8(uint8_t a);
extern uint16_t asm_callee_u16(uint16_t a);
extern uint8_t * asm_callee_ptr(uint8_t *p);
extern uint32_t asm_callee_u32(uint32_t a);
extern unsigned _BitInt(40) asm_callee_u40(unsigned _BitInt(40) a);
extern unsigned _BitInt(48) asm_callee_u48(unsigned _BitInt(48) a);
extern unsigned _BitInt(56) asm_callee_u56(unsigned _BitInt(56) a);
extern uint64_t asm_callee_u64(uint64_t a);
extern __bit asm_callee_bit_ret_set(void);
extern __bit asm_callee_bit_ret_clr(void);
extern __bit asm_callee_bit_param_nr(__bit a);
extern __bit asm_callee_bit_param_reent(__bit a) __reentrant;
extern uint8_t asm_caller_call_c_u32(void);

uint32_t c_callee_u32(uint32_t a) {
    /* Assert argument arrived in A:B:DPH:DPL as 0x11223344 */
    if (a != 0x11223344UL) return 0;
    return a + 0x11111111UL; /* returns 0x22334455 */
}

__xdata uint8_t g_asm_test_buf[8] = {10, 20, 30, 40, 50, 60, 70, 80};

void main(void) {
    test_init();

    /* 1. 1-byte scalar slot (DPL) */
    uint8_t r8 = asm_callee_u8(0x41);
    ASSERT(r8 == 0x42);

    /* 2. 2-byte scalar slot (DPH:DPL) */
    uint16_t r16 = asm_callee_u16(0x1020);
    ASSERT(r16 == (0x1020 + 0x0102));

    /* 3. 3-byte pointer slot (B:DPH:DPL) */
    uint8_t *p = asm_callee_ptr(g_asm_test_buf);
    ASSERT(p == (g_asm_test_buf + 3));
    ASSERT(*p == 40);

    /* 4. 4-byte scalar slot (A:B:DPH:DPL) */
    uint32_t r32 = asm_callee_u32(0x10203040UL);
    ASSERT(r32 == (0x10203040UL + 0x01000001UL));

    /* 5. 5-byte scalar slot (_BitInt(40): DPL, DPH, B, A, R4) */
    unsigned _BitInt(40) v40 = 0x1020304050ULL;
    unsigned _BitInt(40) r40 = asm_callee_u40(v40);
    ASSERT(r40 == (v40 + 0x0500000001ULL));

    /* 6. 6-byte scalar slot (_BitInt(48): DPL, DPH, B, A, R4, R5) */
    unsigned _BitInt(48) v48 = 0x102030405060ULL;
    unsigned _BitInt(48) r48 = asm_callee_u48(v48);
    ASSERT(r48 == (v48 + 0x060000000001ULL));

    /* 7. 7-byte scalar slot (_BitInt(56): DPL, DPH, B, A, R4, R5, R6) */
    unsigned _BitInt(56) v56 = 0x10203040506070ULL;
    unsigned _BitInt(56) r56 = asm_callee_u56(v56);
    ASSERT(r56 == (v56 + 0x07000000000001ULL));

    /* 8. 8-byte scalar slot (uint64_t: DPL, DPH, B, A, R4, R5, R6, R7) */
    uint64_t v64 = 0x0102030405060708ULL;
    uint64_t r64 = asm_callee_u64(v64);
    ASSERT(r64 == (v64 + 0x0800000000000001ULL));

    /* 9. Bit return via CY flag */
    __bit b1 = asm_callee_bit_ret_set();
    ASSERT(b1 == 1);
    __bit b0 = asm_callee_bit_ret_clr();
    ASSERT(b0 == 0);

    /* 10. Ordinary mode bit parameter via BSEG _PARM_1 (non-stack-auto) */
#if !defined(__SDCC_STACK_AUTO)
    __bit bp_nr1 = asm_callee_bit_param_nr(1);
    ASSERT(bp_nr1 == 0);
    __bit bp_nr0 = asm_callee_bit_param_nr(0);
    ASSERT(bp_nr0 == 1);
#endif

    /* 11. Reentrant/Stack-Auto mode bit parameter via b0 bit register */
    __bit bp_r1 = asm_callee_bit_param_reent(1);
    ASSERT(bp_r1 == 0);
    __bit bp_r0 = asm_callee_bit_param_reent(0);
    ASSERT(bp_r0 == 1);

    /* 12. Hand-written ASM Caller calls C Callee */
    uint8_t caller_res = asm_caller_call_c_u32();
    ASSERT(caller_res == 1);

    test_pass();
}
