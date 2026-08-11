/*
 * spi_master.c - STC32G12K128 SPI master example for SDCC mcs251.
 *
 * Demonstrates the on-chip SPI peripheral in master mode, exchanging
 * bytes with an external slave (e.g. a W25Q-series Flash, an SD card
 * socket, or a sensor).  The /SS pin is driven in software so any GPIO
 * can be the chip-select.
 *
 * SPI register summary (all traditional SFR on STC32G12K128):
 *   SPSTAT (0xCD)  status:   SPIF (transfer done), WCOL (write collision)
 *   SPCTL  (0xCE)  control:  SSIG SPEN DORD MSTR CPOL CPHA SPR[1:0]
 *   SPDAT  (0xCF)  data:     write to launch a transfer, read to receive
 *
 * SPCTL bit layout:
 *   bit 7  SSIG   = 1  ignore /SS pin (master drives chip-select in SW)
 *   bit 6  SPEN   = 1  enable SPI
 *   bit 5  DORD   = 0  MSB first (0) / LSB first (1)
 *   bit 4  MSTR   = 1  master mode
 *   bit 3  CPOL   = 0  clock idle low (mode 0); set for mode 2/3
 *   bit 2  CPHA   = 0  sample on first edge (mode 0); set for mode 1/3
 *   bit 1:0 SPR        clock prescaler: 00=/4, 01=/16, 10=/64, 11=/128
 *
 * The four SPI modes (CPOL/CPHA combinations) are the standard Motorola
 * framing; most Flash chips and sensors use mode 0 (CPOL=0, CPHA=0) or
 * mode 3 (CPOL=1, CPHA=1).
 *
 * Build:
 *   sdcc -mmcs251 spi_master.c
 *
 * The simulator (uCsim) does not model the SPI shift register, so a
 * transfer appears to complete instantly with SPDAT unchanged; on real
 * STC32G12K128 silicon this example exchanges bytes with a real slave.
 */

#include <stc32g12k128.h>

/* SPCTL value for SPI mode 0, master, MSB-first, /SS ignored, SYSclk/4.
 *   SSIG(0x80) | SPEN(0x40) | MSTR(0x10) | SPR_4(0x00) = 0xD0 */
#define SPCTL_MASTER_MODE0  (SPCTL_SSIG | SPCTL_SPEN | SPCTL_MSTR)

/* A software chip-select pin (P2.0) for the external slave.  We pulse
 * it low around each transfer.  Any GPIO can be used; adapt to your
 * board wiring. */
#define NSS_PIN  0x01

static void spi_init(void)
{
    /* Drive /SS high (idle) and configure P2.0 as push-pull output. */
    P2 |= NSS_PIN;
    P2M1 &= ~NSS_PIN;
    P2M0 |= NSS_PIN;

    /* Configure MOSI (P2.3), SCLK (P2.5) as push-pull; MISO (P2.4) as
     * input.  The exact pin mapping depends on P_SW1 SPI_S1:S0 bits;
     * the reset mapping is MOSI=P2.3, MISO=P2.4, SCLK=P2.5. */
    P2M1 &= ~0x28;  P2M0 |= 0x28;   /* P2.3, P2.5 push-pull */
    P2M1 |= 0x10;   P2M0 &= ~0x10;  /* P2.4 input */

    SPSTAT = SPSTAT_SPIF | SPSTAT_WCOL;   /* clear any pending flags */
    SPCTL  = SPCTL_MASTER_MODE0;
}

/* Exchange one byte with the slave (full duplex).  Caller owns /SS:
 * keep it low across a multi-byte command+response sequence, because
 * many slaves (e.g. W25Q Flash) abort the command the moment /CS goes
 * high. */
static unsigned char spi_transfer(unsigned char out)
{
    SPSTAT = SPSTAT_SPIF;      /* clear the done flag */
    SPDAT  = out;              /* launch the transfer */

    while (!(SPSTAT & SPSTAT_SPIF))
        ;                      /* wait for 8 bits to shift through */

    return SPDAT;              /* read the byte shifted in */
}

void main(void)
{
    unsigned char status, id;

    /* Configure P0 as push-pull output so we can echo a status byte. */
    P0M1 = 0x00;  P0M0 = 0xFF;

    spi_init();

    /* Example: read the JEDEC ID of a W25Q Flash (opcode 0x9F).
     * /SS must stay asserted across the opcode and the dummy bytes
     * the master clocks out while the slave returns the ID — a
     * W25Q terminates the command on the rising edge of /CS, so
     * splitting this into three separate transactions would read
     * garbage. */
    P2 &= ~NSS_PIN;            /* assert /SS for the whole transaction */
    (void)spi_transfer(0x9F);  /* JEDEC ID command */
    id     = spi_transfer(0x00); /* dummy tx, rx = manufacturer ID */
    status = spi_transfer(0x00); /* dummy tx, rx = device ID */
    P2 |= NSS_PIN;             /* release /SS */

    P0 = (unsigned char)(id ^ status);

    for (;;)
        ;
}
