/*-------------------------------------------------------------------------
   stc32g12k128.h - Register Declarations for STC32G12K128 (MCS-251 core)

   Written for the SDCC mcs251 backend.  Based on public STC32G documentation
   (STC32G12K128 data sheet, available at stcmicro.com) and the Intel
   8XC251SB User's Manual.  This is a community-written header, not derived
   from any proprietary compiler's header file.

   SFR addresses are STC32G12K128-specific; they differ from STC89/12 because
   STC32G uses the MCS-251 core and a different SFR map.  Covers: GPIO P0-P7
   (with mode registers), Timer 0-4, UART1-4, SPI, I2C, ADC, PCA, interrupts,
   watchdog (WDT_CONTR) and IAP/EEPROM (IAP_DATA/ADDRH/ADDRL/CMD/TRIG/CONTR).
   Extended peripherals live in xdata space at EAXFR_BASE (0x7E0000) and
   require P_SW2 |= P_SW2_EAXFR before access.

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.
-------------------------------------------------------------------------*/

#ifndef __STC32G12K128_H__
#define __STC32G12K128_H__

/* MCS-251 core SFRs (present on all 251 variants) */
__sfr __at (0x80) P0;
__sfr __at (0x81) SP;       /* legacy 8-bit stack pointer (unused on 251; SPX is active) */
__sfr __at (0x82) DPL;
__sfr __at (0x83) DPH;
__sfr __at (0x84) DPXL;     /* DPX bits 16-23; reset value 0x01 */
__sfr __at (0x86) SPH;      /* legacy SP high byte */
__sfr __at (0x87) PCON;
__sfr __at (0x88) TCON;
__sfr __at (0x89) TMOD;
__sfr __at (0x8A) TL0;
__sfr __at (0x8B) TL1;
__sfr __at (0x8C) TH0;
__sfr __at (0x8D) TH1;
__sfr __at (0x8E) AUXR;     /* STC auxiliary register */
__sfr __at (0x8F) INTCLKO;  /* interrupt clock output */
__sfr __at (0x90) P1;
__sfr __at (0x91) P1M1;
__sfr __at (0x92) P1M0;
__sfr __at (0x93) P0M1;
__sfr __at (0x94) P0M0;
__sfr __at (0x95) P2M1;
__sfr __at (0x96) P2M0;
__sfr __at (0x97) AUXR2;    /* STC auxiliary register 2 (Timer2 control bits) */
__sfr __at (0x98) SCON;     /* UART1 control */
__sfr __at (0x99) SBUF;     /* UART1 data buffer */
__sfr __at (0xA0) P2;
__sfr __at (0xA1) BUS_SPEED;/* external memory bus speed (for parallel bus) */
__sfr __at (0xA2) P_SW1;    /* peripheral switch 1 (UART/SPI pin mapping) */
__sfr __at (0xAA) WKTCL;    /* wake-up timer low byte (power-down wake) */
__sfr __at (0xAB) WKTCH;    /* wake-up timer high byte + enable */
__sfr __at (0xAE) TA;       /* timed-access key (write 0x55,0xAA to unlock) */
__sfr __at (0xA8) IE;       /* interrupt enable */
__sfr __at (0xA9) IE2;
__sfr __at (0xB0) P3;
__sfr __at (0xB1) P3M1;
__sfr __at (0xB2) P3M0;
__sfr __at (0xB3) P4M1;
__sfr __at (0xB4) P4M0;
__sfr __at (0xB5) IP2;      /* interrupt priority 2 (SPI/UART2/ext-int4) */
__sfr __at (0xB6) IP2H;     /* interrupt priority 2 high byte */
__sfr __at (0xB7) IPH0;     /* interrupt priority high */
__sfr __at (0xB8) IPL0;     /* interrupt priority low (IP) */
__sfr __at (0xBA) P_SW2;    /* peripheral switch 2 (EAXFR bit enables xdata SFR access) */
__sfr __at (0xBB) P_SW3;    /* peripheral switch 3 (CAN pin mapping) */
__sfr __at (0xC0) P4;
/* Watchdog timer and IAP (In-Application Programming / EEPROM) registers.
   These occupy the traditional SFR block 0xC1-0xC7 that STC has used
   consistently since the STC89/STC12/STC15 families; STC32G12K128 keeps
   the same addresses.  EEPROM access is performed entirely through IAP:
   load IAP_ADDRH/IAP_ADDRL + IAP_CMD, then write 0x5A followed by 0xA5
   to IAP_TRIG to launch the command.  See STC32G data sheet chapter
   "EEPROM/IAP" for the command encoding and timing-control bit field. */
__sfr __at (0xC1) WDT_CONTR;  /* watchdog control (enable, prescaler, idle-count) */
__sfr __at (0xC2) IAP_DATA;   /* IAP data (read/write byte) */
__sfr __at (0xC3) IAP_ADDRH;  /* IAP address high byte */
__sfr __at (0xC4) IAP_ADDRL;  /* IAP address low byte */
__sfr __at (0xC5) IAP_CMD;    /* IAP command: 0=idle, 1=read, 2=write, 3=erase */
__sfr __at (0xC6) IAP_TRIG;   /* IAP trigger: write 0x5A then 0xA5 to execute */
__sfr __at (0xC7) IAP_CONTR;  /* IAP control (IAPEN enable, SWBS, SWRST, wait time) */
__sfr __at (0xC8) P5;
__sfr __at (0xC9) P5M1;
__sfr __at (0xCA) P5M0;
__sfr __at (0xCB) P6M1;
__sfr __at (0xCC) P6M0;
__sfr __at (0xDF) IP3;      /* interrupt priority 3 (UART3/4) */
__sfr __at (0xD0) PSW;      /* 251 PSW (CY/AC/F0/RS1/RS0/OV/F1/P) */
__sfr __at (0xD1) PSW1;     /* 251 PSW1 (bit7=N, bit6=Z, ...) */
__sfr __at (0xD8) CCON;     /* PCA control (251 bit-addressable) */
__sfr __at (0xE0) ACC;      /* 8051 accumulator (compiler-reserved on mcs251) */
__sfr __at (0xE1) P7M1;
__sfr __at (0xE2) P7M0;
__sfr __at (0xE3) DPS;      /* data-pointer select (0=DPTR0, 1=DPTR1) */
__sfr __at (0xE4) DPL1;     /* second DPTR low byte */
__sfr __at (0xE5) DPH1;     /* second DPTR high byte */
__sfr __at (0xE8) P6;
__sfr __at (0xE9) WTST;     /* wait-state control (0 = fastest XRAM access) */
__sfr __at (0xEA) CKCON;    /* clock control (high-speed XRAM/SFR access) */
__sfr __at (0xEB) MXAX;     /* extended address MUX (mcs51 legacy pdata paging) */
__sfr __at (0xEE) IP3H;     /* interrupt priority 3 high byte */
__sfr __at (0xF0) B;        /* 8051 B register (mul/div second operand) */
__sfr __at (0xF8) P7;
__sfr __at (0xFF) RSTCFG;   /* reset configuration (ENCLKLVL, BOOT/ISP options) */

/* -------------------------------------------------------------------------
   STC32G12K128 peripheral SFR map.

   STC32G12K128 keeps the 8051 traditional SFR block (0x80-0xFF) for the
   commonly used peripherals (ADC, SPI, UART2/3/4, Timer2/3/4, comparator,
   extra IAP fields).  These are accessed with direct addressing just like
   P0/TCON/SCON above, NOT through the extended SFR window.

   The extended SFR area (0x7E0000+, "EAXFR space") holds the newer
   peripherals and pin-configuration registers: I2C, timer pre-scalers,
   clock control, GPIO pull-up / drive-strength / slew-rate, advanced
   PWM (PWMA/PWMB), DMA, CAN.  Accessing any of these requires setting
   P_SW2.EAXFR (P_SW2 |= P_SW2_EAXFR) first.
   ------------------------------------------------------------------------- */

/* --- ADC (12-bit, traditional SFR) -------------------------------------- */
__sfr __at (0xBC) ADC_CONTR;  /* ADC control: ADC_POWER/START/FLAG/EPWMT + chan */
__sfr __at (0xBD) ADC_RES;    /* ADC result high byte (or low, per RESFMT) */
__sfr __at (0xBE) ADC_RESL;   /* ADC result low byte */
__sfr __at (0xDE) ADCCFG;     /* ADC config: SPEED[7:5], RESFMT(bit5 align) */

#define ADC_CONTR_ADC_POWER 0x80   /* bit 7: ADC power on */
#define ADC_CONTR_ADC_START 0x40   /* bit 6: start a conversion */
#define ADC_CONTR_ADC_FLAG  0x20   /* bit 5: conversion-complete flag */
#define ADC_CONTR_ADC_EPWMT 0x10   /* bit 4: PWM-trigger conversion enable */
#define ADC_CONTR_ADC_CHS   0x0F   /* bits [3:0]: analogue channel select */

#define ADCCFG_RESFMT       0x20   /* bit 5: 0=left-justified, 1=right-justified */
#define ADCCFG_SPEED_MASK   0xE0   /* bits [7:5]: ADC clock = SYSclk/2/(SPEED+1) */

/* ADC interrupt enable lives in the standard IE register (bit 5). */
#define IE_EADC             0x20   /* IE.5: ADC interrupt enable (8051 standard) */

#define ADC_VECTOR          5      /* ADC interrupt vector */

/* --- SPI (traditional SFR) ---------------------------------------------- */
__sfr __at (0xCD) SPSTAT;     /* SPI status */
__sfr __at (0xCE) SPCTL;      /* SPI control */
__sfr __at (0xCF) SPDAT;      /* SPI data */

#define SPSTAT_SPIF 0x80       /* bit 7: SPI transfer-complete flag */
#define SPSTAT_WCOL 0x40       /* bit 6: write-collision flag */
#define SPCTL_SSIG  0x80       /* bit 7: SS ignore (master drives /SS in SW) */
#define SPCTL_SPEN  0x40       /* bit 6: SPI enable */
#define SPCTL_DORD  0x20       /* bit 5: data order (0=MSB first) */
#define SPCTL_MSTR  0x10       /* bit 4: master mode */
#define SPCTL_CPOL  0x08       /* bit 3: clock polarity */
#define SPCTL_CPHA  0x04       /* bit 2: clock phase */
#define SPCTL_SPR_MASK 0x03    /* bits [1:0]: clock prescaler */
#define SPCTL_SPR_4    0x00    /* SYSclk/4   */
#define SPCTL_SPR_16   0x01    /* SYSclk/16  */
#define SPCTL_SPR_64   0x02    /* SYSclk/64  */
#define SPCTL_SPR_128  0x03    /* SYSclk/128 */

/* --- UART2/3/4 (traditional SFR) ---------------------------------------- */
__sfr __at (0x9A) S2CON;      /* UART2 control */
__sfr __at (0x9B) S2BUF;      /* UART2 data buffer */
__sfr __at (0xAC) S3CON;      /* UART3 control */
__sfr __at (0xAD) S3BUF;      /* UART3 data buffer */
__sfr __at (0xFD) S4CON;      /* UART4 control */
__sfr __at (0xFE) S4BUF;      /* UART4 data buffer */

#define S2CON_S2TI  0x02       /* bit 1: UART2 transmit interrupt flag */
#define S2CON_S2RI  0x01       /* bit 0: UART2 receive interrupt flag */
#define S2CON_S2SM0 0x80       /* bit 7: mode bit 0 */
#define S2CON_S2SM1 0x40       /* bit 6: mode bit 1 */
#define S2CON_S2REN 0x10       /* bit 4: receive enable */

/* --- Timer 2/3/4 (traditional SFR) -------------------------------------- */
/* Timer2 shares the AUXR2 control bit layout of Timer0/1; Timer3/4 are
   governed by the combined T4T3M register. */
__sfr __at (0xD6) T2H;        /* Timer2 high byte */
__sfr __at (0xD7) T2L;        /* Timer2 low byte */
__sfr __at (0xD2) T4H;        /* Timer4 high byte */
__sfr __at (0xD3) T4L;        /* Timer4 low byte */
__sfr __at (0xD4) T3H;        /* Timer3 high byte */
__sfr __at (0xD5) T3L;        /* Timer3 low byte */
__sfr __at (0xDD) T4T3M;      /* Timer4/3 mode: T4R/T4_CT/T4x12/T4CLKO/T3R/... */

#define T4T3M_T4R     0x80     /* bit 7: Timer4 run */
#define T4T3M_T4_CT   0x40     /* bit 6: Timer4 counter mode */
#define T4T3M_T4x12   0x20     /* bit 5: Timer4 1T mode */
#define T4T3M_T4CLKO  0x10     /* bit 4: Timer4 clock output */
#define T4T3M_T3R     0x08     /* bit 3: Timer3 run */
#define T4T3M_T3_CT   0x04     /* bit 2: Timer3 counter mode */
#define T4T3M_T3x12   0x02     /* bit 1: Timer3 1T mode */
#define T4T3M_T3CLKO  0x01     /* bit 0: Timer3 clock output */

/* --- Comparator (traditional SFR) --------------------------------------- */
__sfr __at (0xE6) CMPCR1;     /* comparator control 1 */
__sfr __at (0xE7) CMPCR2;     /* comparator control 2 */

#define CMPCR1_CMPEN  0x80     /* bit 7: comparator enable */
#define CMPCR1_CMPIF  0x40     /* bit 6: comparator interrupt flag */
#define CMPCR1_PIE    0x20     /* bit 5: positive-edge interrupt enable */
#define CMPCR1_NIE    0x10     /* bit 4: negative-edge interrupt enable */
#define CMPCR1_CMPOE  0x02     /* bit 1: comparator output enable */

/* --- Extra IAP fields (traditional SFR) --------------------------------- */
__sfr __at (0xF5) IAP_TPS;    /* IAP wait-time (must match SYSclk per data sheet) */
__sfr __at (0xF6) IAP_ADDRE;  /* IAP address extended (bits 16-23 for >64 KiB) */

/* -------------------------------------------------------------------------
   Extended SFR area (0x7E0000+).  Set P_SW2 |= P_SW2_EAXFR before access.
   ------------------------------------------------------------------------- */
#define EAXFR_BASE 0x7e0000

/* Timer pre-scalers (extended SFR) */
__xdata __at (EAXFR_BASE + 0xfea0) volatile unsigned char TM0PS;
__xdata __at (EAXFR_BASE + 0xfea1) volatile unsigned char TM1PS;
__xdata __at (EAXFR_BASE + 0xfea2) volatile unsigned char TM2PS;
__xdata __at (EAXFR_BASE + 0xfea3) volatile unsigned char TM3PS;
__xdata __at (EAXFR_BASE + 0xfea4) volatile unsigned char TM4PS;

/* ADC internal timing (extended SFR).  Program ADCTIM before enabling
   ADC_POWER; the data-sheet reset value 0x3F works for the common
   22-30 MHz clock range. */
__xdata __at (EAXFR_BASE + 0xfea8) volatile unsigned char ADCTIM;

/* I2C (extended SFR) */
__xdata __at (EAXFR_BASE + 0xfe80) volatile unsigned char I2CCFG;
__xdata __at (EAXFR_BASE + 0xfe81) volatile unsigned char I2CMSCR;
__xdata __at (EAXFR_BASE + 0xfe82) volatile unsigned char I2CMSST;
__xdata __at (EAXFR_BASE + 0xfe83) volatile unsigned char I2CSLCR;
__xdata __at (EAXFR_BASE + 0xfe84) volatile unsigned char I2CSLCST;
__xdata __at (EAXFR_BASE + 0xfe85) volatile unsigned char I2CTXD;
__xdata __at (EAXFR_BASE + 0xfe86) volatile unsigned char I2CRXD;
__xdata __at (EAXFR_BASE + 0xfe87) volatile unsigned char I2CMSAUX;
#define I2CCFG_ENI2C    0x80   /* bit 7: I2C enable */
#define I2CCFG_MSSL     0x40   /* bit 6: master (1) / slave (0) */

/* Timed-access key: certain protected SFRs (WDT_CONTR, IAP_CONTR,
 * P_SWx bits, RSTCFG) can only be written within three machine cycles
 * of writing 0x55 then 0xA5 to TA.  Helper macro for clarity. */
#define TA_UNLOCK()     do { TA = 0x55; TA = 0xAA; } while (0)

/* Interrupt priority registers.  STC32G uses four priority levels via
 * paired low/high bit registers: level = (H:L) where 00=lowest..11=highest.
 * IP/IPL0+IPH0 cover the basic 8051 sources; IP2/IP2H the second bank
 * (SPI, UART2, ext-int4); IP3/IP3H the third bank (UART3/4). */
#define IP_PX0    0x01   /* IPL0 bit 0: ext int 0 priority low */
#define IP_PT0    0x02   /* IPL0 bit 1: Timer0 priority low */
#define IP_PX1    0x04   /* IPL0 bit 2: ext int 1 priority low */
#define IP_PT1    0x08   /* IPL0 bit 3: Timer1 priority low */
#define IP_PS     0x10   /* IPL0 bit 4: UART1 priority low */
#define IP_PADC   0x20   /* IPL0 bit 5: ADC priority low */
#define IP_PLVD   0x40   /* IPL0 bit 6: low-voltage detect priority low */
#define IPH0_MASK 0x7F   /* IPH0 mirrors the same bits for the high half */
#define IP2_PS2   0x01   /* IP2 bit 0: UART2 priority low */
#define IP2_PSPI  0x02   /* IP2 bit 1: SPI priority low */
#define IP2_PX4   0x10   /* IP2 bit 4: ext int 4 priority low */
#define IP3_PS3   0x01   /* IP3 bit 0: UART3 priority low */
#define IP3_PS4   0x02   /* IP3 bit 1: UART4 priority low */

/* P_SW1 pin-mapping bits for SPI and UART1 routing. */
#define P_SW1_SPI_S0  0x04   /* P_SW1 bit 2: SPI pin select bit 0 */
#define P_SW1_SPI_S1  0x08   /* P_SW1 bit 3: SPI pin select bit 1 */

/* P_SW2 pin-mapping bits (EAXFR + UART2/3/4 + I2C routing). */
#define P_SW2_S2_S    0x01   /* P_SW2 bit 0: UART2 pin select */
#define P_SW2_S3_S    0x02   /* P_SW2 bit 1: UART3 pin select */
#define P_SW2_S4_S    0x04   /* P_SW2 bit 2: UART4 pin select */
#define P_SW2_I2C_S0  0x10   /* P_SW2 bit 4: I2C pin select bit 0 */
#define P_SW2_I2C_S1  0x20   /* P_SW2 bit 5: I2C pin select bit 1 */
/* (P_SW2_EAXFR 0x80 is already defined above near P_SW2.) */

/* Interrupt enable 2 (IE2 bit definitions, byte 0xAF) */
#define IE2_ES2  0x01   /* bit 0: UART2 interrupt enable */
#define IE2_ESPI 0x02   /* bit 1: SPI interrupt enable */
#define IE2_ET2  0x04   /* bit 2: Timer2 interrupt enable */
#define IE2_ES3  0x08   /* bit 3: UART3 interrupt enable */
#define IE2_ES4  0x10   /* bit 4: UART4 interrupt enable */
#define IE2_ET3  0x20   /* bit 5: Timer3 interrupt enable */
#define IE2_ET4  0x40   /* bit 6: Timer4 interrupt enable */
#define IE2_EUSB 0x80   /* bit 7: USB interrupt enable */

/* Interrupt vector addresses for __interrupt() */
#define INT0_VECTOR    0    /* External Interrupt 0 */
#define TIMER0_VECTOR  1    /* Timer 0 */
#define INT1_VECTOR    2    /* External Interrupt 1 */
#define TIMER1_VECTOR  3    /* Timer 1 */
#define UART1_VECTOR   4    /* UART1 */
#define ADC_VECTOR     5    /* ADC */
#define LVD_VECTOR     6    /* Low Voltage Detect */
#define PCA_VECTOR     7    /* PCA */
#define UART2_VECTOR   8    /* UART2 */
#define SPI_VECTOR     9    /* SPI */
#define INT2_VECTOR   10    /* External Interrupt 2 */
#define INT3_VECTOR   11    /* External Interrupt 3 */
#define TIMER2_VECTOR 12    /* Timer 2 */

/* --- Bit definitions for commonly used SFRs --- */

/* TCON */
#define TCON_IT0 0x01
#define TCON_IE0 0x02
#define TCON_IT1 0x04
#define TCON_IE1 0x08
#define TCON_TR0 0x10
#define TCON_TF0 0x20
#define TCON_TR1 0x40
#define TCON_TF1 0x80

/* IE */
#define IE_EX0 0x01
#define IE_ET0 0x02
#define IE_EX1 0x04
#define IE_ET1 0x08
#define IE_ES  0x10
#define IE_EA  0x80

/* SCON */
#define SCON_RI  0x01
#define SCON_TI  0x02
#define SCON_RB8 0x04
#define SCON_TB8 0x08
#define SCON_REN 0x10
#define SCON_SM2 0x20
#define SCON_SM1 0x40
#define SCON_SM0 0x80

/* P_SW2 */
#define P_SW2_EAXFR 0x80   /* enable access to extended SFR area (xdata 0x7E0000+) */

/* PSW */
#define PSW_CY 0x80
#define PSW_AC 0x40
#define PSW_F0 0x20
#define PSW_RS1 0x10
#define PSW_RS0 0x08
#define PSW_OV 0x04
#define PSW_P  0x01

/* PSW1 */
#define PSW1_N 0x80
#define PSW1_Z 0x40

/* GPIO mode helpers: PxM1/PxM0 bit pairs define pin mode.
   (M1:M0) 00 quasi-bidirectional, 01 push-pull, 10 input-only, 11 open-drain */
#define GPIO_MODE_QUASI  0x00
#define GPIO_MODE_PUSHPULL 0x01
#define GPIO_MODE_INPUT  0x02
#define GPIO_MODE_OPENDRAIN 0x03

/* NOP() emits a single CPU cycle (MCS-251 opcode 0x00 = NOP).  Useful as
   a timing filler or to let an external engine settle after a trigger. */
#define NOP() __asm NOP __endasm

/* IAP_CMD command codes (STC32G data sheet, EEPROM/IAP chapter) */
#define IAP_CMD_IDLE  0x00
#define IAP_CMD_READ  0x01
#define IAP_CMD_WRITE 0x02
#define IAP_CMD_ERASE 0x03

/* IAP_CONTR bit fields (STC32G layout — differs from STC15!).
   STC32G moved the EEPROM wait-time out of IAP_CONTR into its own
   register, IAP_TPS (0xF5).  Set IAP_TPS to the system clock in MHz
   before launching a command (e.g. 24 MHz -> IAP_TPS = 24). */
#define IAP_CONTR_IAPEN    0x80   /* bit 7: enable IAP/EEPROM access */
#define IAP_CONTR_SWBS     0x40   /* bit 6: boot select: 0=user Flash, 1=ISP monitor */
#define IAP_CONTR_SWRST    0x20   /* bit 5: software reset */
#define IAP_CONTR_CMD_FAIL 0x10   /* bit 4: set by hardware if last IAP command failed */

/* IAP_TRIG sequence: write these two bytes back-to-back to launch the
   command loaded into IAP_CMD/IAP_ADDR/IAP_DATA. */
#define IAP_TRIG_MAGIC1 0x5A
#define IAP_TRIG_MAGIC2 0xA5

/* WDT_CONTR bit fields */
#define WDT_CONTR_WDT_EN  0x20   /* watchdog enable */
#define WDT_CONTR_CLR_WDT 0x10   /* clear watchdog (write 1) */
#define WDT_CONTR_IDLE_WDT 0x08  /* keep counting in idle mode */
#define WDT_CONTR_PS_MASK  0x07  /* prescaler bits [2:0] */

#endif /* __STC32G12K128_H__ */
