/* test_abi_isr.c - Test 4-byte hardware interrupt frame, leaf/non-leaf ISR execution and RETI */
#include "abi_test.h"

__sfr __at (0x84) DPXL;
__sfr __at (0x88) TCON;
__sfr __at (0xA8) IE;
__sfr __at (0xD1) PSW1;

__data volatile uint8_t g_leaf_fired = 0;
__data volatile uint8_t g_nonleaf_fired = 0;
__data volatile uint8_t g_saved_psw1 = 0;
__data volatile uint8_t g_saved_dpxl = 0;
__data volatile uint8_t g_dr_fired = 0;
volatile __xdata uint8_t g_far_storage[8] = {0xA5,0,0,0,0,0,0,0};
__data volatile uint8_t g_dr24_b0 = 0;
__data volatile uint8_t g_dr24_b1 = 0;
__data volatile uint8_t g_dr24_b2 = 0;
__data volatile uint8_t g_dr28_b0 = 0;
__data volatile uint8_t g_dr28_b1 = 0;
__data volatile uint8_t g_dr28_b2 = 0;

typedef struct { uint16_t a; uint16_t b; } DrPair;
volatile __xdata uint16_t g_dr_sink;
volatile __xdata uint8_t * volatile g_far_src;
volatile __xdata uint8_t * volatile g_far_dst;
__data volatile uint8_t g_dr_bad = 0;

/* Three independent clobber sources, one per raw temporary:
   - DR28: aggregate return copies the hidden destination pointer and the
     source pointer through DR28 (gen.c raw temporaries);
   - DR24: __setjmp uses DR24 as the register-dump write pointer
     (device/lib/_setjmp.c: "mov dr24,dpx" + increments);
   - DR20: __setjmp saves the ECALL frame pointer in DR20
     (device/lib/_setjmp.c: "mov dr20,dpx"). */
DrPair dr_make_pair(void) { DrPair p; p.a = 0x1122; p.b = 0x3344; return p; }

void helper_clobber_all(void) __reentrant {
    /* Modify registers and DPXL */
    volatile uint32_t a = 0xAAAAAAAAUL;
    volatile uint32_t b = 0xBBBBBBBBUL;
    volatile uint32_t c = a + b;
    DPXL = 0x12;
    (void)c;
}

/* 1. Leaf ISR: does not call external functions (selective register save) */
void isr_leaf(void) __interrupt (0) {
    g_leaf_fired++;
    /* Clobber ACC, B, DPL, PSW1 */
    PSW1 = 0x00;
}

/* 2. Non-Leaf ISR: calls another function (full bank & fixed registers save including DPXL) */
void isr_non_leaf(void) __interrupt (2) {
    g_nonleaf_fired++;
    helper_clobber_all();
    PSW1 = 0x00;
}

/* 3. Raw-temporary canary ISR (Timer0, vector 1): aggregate return
   (DR28), far-pointer dereference (DR28), setjmp (DR20 + DR24) and a
   helper call all execute inside the ISR while the foreground holds
   live 32-bit patterns in DR20/DR24/DR28. */
#include <setjmp.h>
jmp_buf g_dr_env;
void isr_dr(void) __interrupt (1) {
    DrPair p = dr_make_pair();
    g_far_dst[1] = g_far_src[0];
    (void)setjmp(g_dr_env);
    helper_clobber_all();
    g_dr_sink = p.a + p.b;
    g_dr_fired++;
}

void main(void) {
    test_init();

    /* ================================================================ */
    /* Test 1: Leaf ISR (INT0, vector 0)                                 */
    /* ================================================================ */
    g_leaf_fired = 0;
    PSW1 = 0xC4;  /* CY=1, AC=1, OV=1, RS=0 */

    TCON |= 0x01; /* IT0 edge */
    IE |= 0x81;   /* EA=1, EX0=1 */
    TCON |= 0x02; /* Trigger INT0 */

    g_saved_psw1 = PSW1;
    IE &= ~0x80;
    TCON &= ~0x01;

    ASSERT(g_leaf_fired == 1);
    ASSERT((g_saved_psw1 & 0xC4) == 0xC4);

    /* ================================================================ */
    /* Test 2: Non-Leaf ISR (INT1, vector 2)                             */
    /* ================================================================ */
    g_nonleaf_fired = 0;
    PSW1 = 0xC4;
    DPXL = 0x5A;  /* Set DPXL canary before interrupt */

    TCON |= 0x04; /* IT1 edge */
    IE |= 0x84;   /* EA=1, EX1=1 */
    TCON |= 0x08; /* Trigger INT1 */

    g_saved_psw1 = PSW1;
    g_saved_dpxl = DPXL;
    IE &= ~0x80;
    TCON &= ~0x04;
    DPXL = 0x01;  /* Restore model-large PSEG pointer base */

    ASSERT(g_nonleaf_fired == 1);
    ASSERT((g_saved_psw1 & 0xC4) == 0xC4);
    ASSERT(g_saved_dpxl == 0x5A);

    /* ================================================================ */
    /* Test 3: raw-temporary canary DR20/DR24/DR28, full 32-bit         */
    /* (Timer0, vector 1)                                                */
    /* ================================================================ */
    g_dr_fired = 0;
    g_dr_bad = 0;
    g_far_src = &g_far_storage[0];
    g_far_dst = &g_far_storage[4];
    IE |= 0x82;   /* EA=1, ET0=1 (enable before the patterns load) */

    /* Full 32-bit live patterns (mov sets bits 15:0, movh bits 31:16);
       nothing between the loads and the trigger touches them (the SFR
       trigger below is a read-modify-write on TCON only). */
    __asm
        mov     dr20, #0xC0DE
        movh    dr20, #0xFEED   ; DR20 = 0xFEEDC0DE
        mov     dr24, #0x1111
        movh    dr24, #0x2222   ; DR24 = 0x22221111
        mov     dr28, #0x3333
        movh    dr28, #0x4444   ; DR28 = 0x44443333
    __endasm;

    TCON |= 0x20; /* TF0: software-latch Timer0 interrupt */

    /* The ISR ran to RETI at the next instruction boundary.  Verify each
       register in full 32 bits: rebuild the pattern in DR16 (unused by
       the ISR chain) and branch to a per-register bad marker on !=. */
    __asm
        mov     dr16, #0xC0DE
        movh    dr16, #0xFEED
        cmp     dr16, dr20
        jne     dr20_bad$
        mov     dr16, #0x1111
        movh    dr16, #0x2222
        cmp     dr16, dr24
        jne     dr24_bad$
        mov     dr16, #0x3333
        movh    dr16, #0x4444
        cmp     dr16, dr28
        jne     dr28_bad$
        sjmp    dr_check_done$
dr20_bad$:
        mov     _g_dr_bad, #1
        sjmp    dr_check_done$
dr24_bad$:
        mov     _g_dr_bad, #2
        sjmp    dr_check_done$
dr28_bad$:
        mov     _g_dr_bad, #3
dr_check_done$:
    __endasm;

    IE &= ~0x80;
    TCON &= ~0x20;

    ASSERT(g_dr_fired == 1);
    ASSERT(g_dr_bad == 0);   /* 1=DR20, 2=DR24, 3=DR28 corrupted */
    ASSERT(g_far_dst[1] == 0xA5);
    ASSERT(g_dr_sink == 0x1122u + 0x3344u);

    test_pass();
}
