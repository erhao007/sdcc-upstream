/*-------------------------------------------------------------------------
   stc32g12k128.h - Register Declarations for STC32G12K128 (MCS-251 core)

   Written for the SDCC mcs251 backend.  Based on public STC32G documentation
   (STC32G12K128 data sheet, available at stcmicro.com) and the Intel
   8XC251SB User's Manual.  This is a community-written header, not derived
   from any proprietary compiler's header file.

   SFR addresses are STC32G12K128-specific; they differ from STC89/12 because
   STC32G uses the MCS-251 core and a different SFR map.  Only the commonly
   used peripherals (GPIO, Timer 0/1/2, UART1, interrupts, watchdog) are
   declared here; extend as needed.

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
__xdata __at (EAXFR_BASE + 0xfe23) volatile unsigned char T2CR;   /* Timer2 control */
__xdata __at (EAXFR_BASE + 0xfe24) volatile unsigned char T2CFG;
__xdata __at (EAXFR_BASE + 0xfe7a) volatile unsigned char T2H;
__xdata __at (EAXFR_BASE + 0xfe7b) volatile unsigned char T2L;

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

#endif /* __STC32G12K128_H__ */
