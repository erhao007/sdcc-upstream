/*
 * iap_eeprom.c - STC32G12K128 IAP/EEPROM example for SDCC mcs251.
 *
 * Demonstrates reading, writing and erasing the on-chip EEPROM (which
 * STC calls "IAP Flash") through the IAP_DATA / IAP_ADDRH / IAP_ADDRL /
 * IAP_CMD / IAP_TRIG / IAP_CONTR register block.  STC32G12K128 has up
 * to 128 KiB of EEPROM organised in 512-byte pages; a page must be
 * erased before it is reprogrammed.
 *
 * The IAP wait-time is programmed via the dedicated IAP_TPS register
 * (0xF5) — NOT the IAP_CONTR[2:0] field used by the older STC15.
 * IAP_TPS must be set to the system clock in MHz before any command
 * (e.g. 24 for a 24 MHz clock).  See the STC32G data sheet.
 *
 * Build:
 *   sdcc -mmcs251 iap_eeprom.c
 *
 * The simulator (uCsim) does not model the EEPROM engine, so this is a
 * compile + code-pattern check.  On real STC32G12K128 hardware it reads,
 * rewrites and verifies a test byte.
 */

#include <stc32g12k128.h>

/* Pick an EEPROM address in the first page (0x0000-0x01FF).  The first
 * 512 bytes form one erase page; we use byte offset 0x0040 inside it. */
#define TEST_EEPROM_ADDR  0x0040

/* STC32G moved the EEPROM wait-time into its own register, IAP_TPS (0xF5),
 * which must be programmed with the system clock in MHz before any command.
 * For a 24 MHz clock we write 24. */
#define IAP_TPS_SETUP     24

/* IAP_CONTR setup: enable IAP only.  We never set SWRST (software reset)
 * or SWBS (boot select) in this example.  Bits 3:0 are reserved on
 * STC32G (the wait-time no longer lives here, unlike STC15). */
#define IAP_CONTR_SETUP   IAP_CONTR_IAPEN

/* Launch the IAP command previously loaded into IAP_CMD/IAP_ADDR/IAP_DATA
 * by writing the magic two-byte sequence to IAP_TRIG.  The data sheet
 * requires these two writes to happen back-to-back with no other IAP
 * register access in between. */
static void iap_trigger(void)
{
    /* The trigger must run with interrupts disabled: an ISR firing
     * between the two magic writes would leave the IAP engine armed
     * but unfired, and a later unrelated IAP register access could
     * accidentally launch the wrong command. */
    unsigned char ie_save = IE;
    IE &= ~IE_EA;                 /* disable interrupts (clear EA bit) */
    IAP_TRIG = IAP_TRIG_MAGIC1;   /* 0x5A */
    IAP_TRIG = IAP_TRIG_MAGIC2;   /* 0xA5 */
    NOP();                        /* let the engine settle */
    NOP();
    NOP();
    NOP();
    IE = ie_save;                 /* restore interrupt state */
}

/* Read one byte from EEPROM. */
static unsigned char eeprom_read(unsigned int addr)
{
    unsigned char v;
    IAP_CONTR = IAP_CONTR_SETUP;
    IAP_CMD   = IAP_CMD_READ;
    IAP_ADDRH = (unsigned char)(addr >> 8);
    IAP_ADDRL = (unsigned char)(addr & 0xFF);
    iap_trigger();
    v = IAP_DATA;
    IAP_CMD   = IAP_CMD_IDLE;
    IAP_CONTR = 0;                /* disable IAP when idle (safety) */
    return v;
}

/* Write one byte to EEPROM (the target byte's page must already be erased). */
static void eeprom_write(unsigned int addr, unsigned char v)
{
    IAP_CONTR = IAP_CONTR_SETUP;
    IAP_CMD   = IAP_CMD_WRITE;
    IAP_ADDRH = (unsigned char)(addr >> 8);
    IAP_ADDRL = (unsigned char)(addr & 0xFF);
    IAP_DATA  = v;
    iap_trigger();
    IAP_CMD   = IAP_CMD_IDLE;
    IAP_CONTR = 0;
}

/* Erase a 512-byte EEPROM page (addr may be any byte inside the page). */
static void eeprom_erase_page(unsigned int addr)
{
    IAP_CONTR = IAP_CONTR_SETUP;
    IAP_CMD   = IAP_CMD_ERASE;
    IAP_ADDRH = (unsigned char)(addr >> 8);
    IAP_ADDRL = (unsigned char)(addr & 0xFF);
    iap_trigger();
    IAP_CMD   = IAP_CMD_IDLE;
    IAP_CONTR = 0;
}

void main(void)
{
    unsigned char before, after;

    /* Configure P0.0 as push-pull output so we can signal success/failure. */
    P0M1 &= ~0x01;
    P0M0 |= 0x01;

    /* Program the EEPROM wait-time to match the system clock.  STC32G
     * uses a dedicated IAP_TPS register (not the IAP_CONTR low bits
     * like STC15).  Set this once before any EEPROM command. */
    IAP_TPS = IAP_TPS_SETUP;

    /* SAFETY: on STC32G the user-EEPROM area is a separate region that
     * must first be allocated in the STC-ISP "hardware options" dialog
     * before the chip is flashed.  Writing outside that region hits the
     * main program Flash and can brick the firmware.  TEST_EEPROM_ADDR
     * (0x0040) is inside the first 512-byte EEPROM page — only safe
     * once that EEPROM area has been configured.
     *
     * Read the current value first; only erase+rewrite if it differs
     * from what we want to store, to avoid unnecessary Flash wear. */
    before = eeprom_read(TEST_EEPROM_ADDR);

    /* Write a recognisable pattern, but only after erasing the page.
     * The erase is conditional: skip it if the location already holds
     * 0xFF (erased state) — we can program a byte without erasing if
     * it only clears bits (erased cells read 0xFF, programming drives
     * them toward 0x00). */
    if (before != 0x5A)
    {
        /* Need to change the byte.  If any bit needs to go 0->1 we
         * must erase the whole 512-byte page first. */
        if ((before & 0x5A) != 0x5A)
            eeprom_erase_page(TEST_EEPROM_ADDR);
        eeprom_write(TEST_EEPROM_ADDR, 0x5A);
    }

    /* Read it back.  On real hardware this is 0x5A; the simulator
     * has no EEPROM model so it returns 0. */
    after = eeprom_read(TEST_EEPROM_ADDR);

    /* Signal the result on P0.0:
     *   P0 = 0xA5  -> write+verify succeeded (0x5A came back)
     *   P0 = 0xFF  -> erase worked but write/read not modelled (simulator)
     *   P0 = 0x01  -> bare toggle, EEPROM engine absent
     */
    if (after == 0x5A)
        P0 = 0xA5;
    else if (after == 0xFF)
        P0 = 0xFF;
    else
        P0 = 0x01;

    for (;;)
        ;
}
