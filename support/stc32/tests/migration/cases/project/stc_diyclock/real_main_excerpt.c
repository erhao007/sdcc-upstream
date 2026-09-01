/*
 * De-businessized migration fixture derived from
 * zerog2k/stc_diyclock/src/main.c at the revision in UPSTREAM_REVISION.
 *
 * Copyright (c) 2016 Jens Jensen
 * The applicable MIT notice is retained in LICENSE.upstream.
 *
 * Only the source-level shape needed by MT-4A is retained.  No upstream
 * device header, peripheral implementation, submodule, or binary is used.
 */

#include "clock_app.h"

volatile __bit diyclock_blinker_fast;
volatile unsigned int diyclock_ticks;
static __bit diyclock_loop_gate;

static void diyclock_step_core(void)
{
    ++diyclock_ticks;
    diyclock_blinker_fast = !diyclock_blinker_fast;
    diyclock_loop_gate = 1;
}

/* The upstream no-parentheses suffix is normalized to the SDCC spelling. */
void timer0_isr(void) __interrupt (1) __using (1)
{
    diyclock_step_core();
}

void diyclock_reset(void)
{
    diyclock_blinker_fast = 0;
    diyclock_ticks = 0;
    diyclock_loop_gate = 0;
}

void diyclock_step(void)
{
    diyclock_step_core();
}

unsigned char diyclock_snapshot(void)
{
    unsigned char snapshot = (unsigned char)diyclock_ticks;

    if (diyclock_blinker_fast)
        snapshot |= 0x10;
    if (diyclock_loop_gate)
        snapshot |= 0x20;
    return snapshot;
}
