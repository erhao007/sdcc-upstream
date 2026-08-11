/*
 * iap_eeprom.c - STC32G12K128 IAP/EEPROM example for SDCC mcs251.
 *
 * Demonstrates reading, writing and erasing the on-chip EEPROM (which
 * STC calls "IAP Flash") through the IAP_DATA / IAP_ADDRH / IAP_ADDRL /
 * IAP_CMD / IAP_TRIG / IAP_CONTR register block.  STC32G12K128 has up
 * to 128 KiB of EEPROM organised in 512-byte pages; a page must be
 * erased before it is reprogrammed.
 *
 * The IAP wait-time field (IAP_CONTR[2:0]) must be set to match the
 * system clock.  See the STC32G data sheet table; for the common
 * 22.1184 MHz / 24 MHz / 30 MHz clock values used below the field is 2.
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

/* IAP_CONTR wait-time field for a ~24 MHz clock (data sheet table:
 * SYSclk=24MHz -> IAP_TPS=4, encoded as the low nibble). */
#define IAP_TPS_24MHZ     0x04

/* Initial IAP_CONTR value: enable IAP + set the wait time.  We never
 * set SWRST (software reset) or SWBS (boot select) in this example. */
#define IAP_CONTR_SETUP   (IAP_CONTR_IAPEN | IAP_TPS_24MHZ)

/* Launch the IAP command previously loaded into IAP_CMD/IAP_ADDR/IAP_DATA
 * by writing the magic two-byte sequence to IAP_TRIG.  The data sheet
 * requires these two writes to happen back-to-back with no other IAP
 * register access in between. */
static void iap_trigger(void)
{
    IAP_TRIG = IAP_TRIG_MAGIC1;   /* 0x5A */
    IAP_TRIG = IAP_TRIG_MAGIC2;   /* 0xA5 */
    NOP();                        /* allow the engine to settle */
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

    /* 1. Read what is currently at the test address. */
    before = eeprom_read(TEST_EEPROM_ADDR);

    /* 2. Erase the page (erased EEPROM reads as 0xFF). */
    eeprom_erase_page(TEST_EEPROM_ADDR);

    /* 3. Write a recognisable pattern byte. */
    eeprom_write(TEST_EEPROM_ADDR, 0x5A);

    /* 4. Read it back.  On real hardware this is 0x5A; the simulator
     *    has no EEPROM model so it returns 0. */
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
