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
__sfr __at (0x98) SCON;     /* UART1 control */
__sfr __at (0x99) SBUF;     /* UART1 data buffer */
__sfr __at (0xA0) P2;
__sfr __at (0xA8) IE;       /* interrupt enable */
__sfr __at (0xA9) IE2;
__sfr __at (0xB0) P3;
__sfr __at (0xB1) P3M1;
__sfr __at (0xB2) P3M0;
__sfr __at (0xB3) P4M1;
__sfr __at (0xB4) P4M0;
__sfr __at (0xB7) IPH0;     /* interrupt priority high */
__sfr __at (0xB8) IPL0;     /* interrupt priority low (IP) */
__sfr __at (0xBA) P_SW2;    /* peripheral switch 2 (EAXFR bit enables xdata SFR access) */
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
__sfr __at (0xD0) PSW;      /* 251 PSW (CY/AC/F0/RS1/RS0/OV/F1/P) */
__sfr __at (0xD1) PSW1;     /* 251 PSW1 (bit7=N, bit6=Z, ...) */
__sfr __at (0xD8) CCON;     /* PCA control (251 bit-addressable) */
__sfr __at (0xE8) P6;
__sfr __at (0xE1) P7M1;
__sfr __at (0xE2) P7M0;
__sfr __at (0xF8) P7;

/* Timer pre-scalers and extended SFRs are mapped in xdata space when
   P_SW2.EAXFR (0x80) is set.  Base 0x7E0000 is the STC32G extended SFR area. */
#define EAXFR_BASE 0x7e0000
__xdata __at (EAXFR_BASE + 0xfea0) volatile unsigned char TM0PS;
__xdata __at (EAXFR_BASE + 0xfea1) volatile unsigned char TM1PS;
__xdata __at (EAXFR_BASE + 0xfea2) volatile unsigned char TM2PS;
__xdata __at (EAXFR_BASE + 0xfea3) volatile unsigned char TM3PS;
__xdata __at (EAXFR_BASE + 0xfea4) volatile unsigned char TM4PS;
__xdata __at (EAXFR_BASE + 0xfe23) volatile unsigned char T2CR;   /* Timer2 control */
__xdata __at (EAXFR_BASE + 0xfe24) volatile unsigned char T2CFG;
__xdata __at (EAXFR_BASE + 0xfe7a) volatile unsigned char T2H;
__xdata __at (EAXFR_BASE + 0xfe7b) volatile unsigned char T2L;

/* Timer 3/4 (extended, accessed via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfe40) volatile unsigned char T4CR;
__xdata __at (EAXFR_BASE + 0xfe41) volatile unsigned char T4CFG;
__xdata __at (EAXFR_BASE + 0xfe42) volatile unsigned char T3CR;
__xdata __at (EAXFR_BASE + 0xfe43) volatile unsigned char T3CFG;
__xdata __at (EAXFR_BASE + 0xfe6a) volatile unsigned char T3H;
__xdata __at (EAXFR_BASE + 0xfe6b) volatile unsigned char T3L;
__xdata __at (EAXFR_BASE + 0xfe6c) volatile unsigned char T4H;
__xdata __at (EAXFR_BASE + 0xfe6d) volatile unsigned char T4L;

/* UART2 (extended SFR via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfe70) volatile unsigned char S2CON;
__xdata __at (EAXFR_BASE + 0xfe71) volatile unsigned char S2BUF;
#define S2CON_S2TI  0x02
#define S2CON_S2RI  0x01
#define S2CON_S2SM0 0x80
#define S2CON_S2SM1 0x40
#define S2CON_S2REN 0x10

/* UART3 (extended SFR via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfe74) volatile unsigned char S3CON;
__xdata __at (EAXFR_BASE + 0xfe75) volatile unsigned char S3BUF;

/* UART4 (extended SFR via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfe78) volatile unsigned char S4CON;
__xdata __at (EAXFR_BASE + 0xfe79) volatile unsigned char S4BUF;

/* SPI (extended SFR via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfee0) volatile unsigned char SPSTAT;
__xdata __at (EAXFR_BASE + 0xfee1) volatile unsigned char SPCTL;
__xdata __at (EAXFR_BASE + 0xfee2) volatile unsigned char SPDAT;
#define SPSTAT_SPIF 0x80
#define SPSTAT_WCOL 0x40
#define SPCTL_SSPEN 0x40
#define SPCTL_MSTR  0x10

/* I2C (extended SFR via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfe80) volatile unsigned char I2CCFG;
__xdata __at (EAXFR_BASE + 0xfe81) volatile unsigned char I2CMSCR;
__xdata __at (EAXFR_BASE + 0xfe82) volatile unsigned char I2CMSST;
__xdata __at (EAXFR_BASE + 0xfe83) volatile unsigned char I2CSLCR;
__xdata __at (EAXFR_BASE + 0xfe84) volatile unsigned char I2CSLCST;
__xdata __at (EAXFR_BASE + 0xfe85) volatile unsigned char I2CTXD;
__xdata __at (EAXFR_BASE + 0xfe86) volatile unsigned char I2CRXD;
__xdata __at (EAXFR_BASE + 0xfe87) volatile unsigned char I2CMSAUX;
#define I2CCFG_ENI2C    0x80
#define I2CCFG_MSSL     0x40

/* ADC (extended SFR via EAXFR) */
__xdata __at (EAXFR_BASE + 0xfe00) volatile unsigned char ADC_CONTR;
__xdata __at (EAXFR_BASE + 0xfe01) volatile unsigned char ADC_CFG;
__xdata __at (EAXFR_BASE + 0xfe02) volatile unsigned char ADC_DAT;  /* low byte */
__xdata __at (EAXFR_BASE + 0xfe03) volatile unsigned char ADC_DATL; /* alternative name */
#define ADC_CONTR_ADC_POWER 0x80
#define ADC_CONTR_ADC_START  0x40
#define ADC_CONTR_ADC_FLAG   0x20
#define ADC_CONTR_ADC_EPAGE  0x0F  /* channel select bits [3:0] */

/* Interrupt enable 2 (IE2 bit definitions) */
#define IE2_ET2  0x04   /* Timer2 interrupt enable */
#define IE2_ESPI 0x40   /* SPI interrupt enable */
#define IE2_ES2  0x01   /* UART2 interrupt enable */

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

/* IAP_CONTR bit fields.  IAPEN enables IAP; the low 3 bits select the
   EEPROM access wait time (IAP_TPS) which must match the system clock:
   IAP_TPS = log2(SYSclk_MHz).  See data sheet table for the mapping. */
#define IAP_CONTR_IAPEN  0x80   /* enable IAP/EEPROM access */
#define IAP_CONTR_SWBS   0x40   /* boot selection: 0=user Flash, 1=ISP monitor */
#define IAP_CONTR_SWRST  0x20   /* software reset */
#define IAP_CONTR_CMD_FAIL 0x10 /* set by hardware if the last IAP command failed */
#define IAP_CONTR_WT_MASK  0x07 /* wait-time selection bits [2:0] */

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
