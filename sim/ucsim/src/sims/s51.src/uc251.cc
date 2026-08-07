/*
 * Simulator of microcontrollers (uc251.cc)
 *
 * Copyright (C) 1999 Drotos Daniel
 *
 * This file is part of microcontroller simulator: ucsim.
 *
 * UCSIM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * UCSIM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with UCSIM; see the file COPYING.  If not, write to the Free
 * Software Foundation, 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA. */
/*@1@*/

//#include "ddconfig.h"

//#include <stdio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "uc251cl.h"

#include "regs51.h"
#include "types51.h"


/*
 * Making an 251 CPU object
 */

cl_uc251::cl_uc251(struct cpu_entry *Itype, class cl_sim *asim):
  cl_uc89c51r(Itype, asim)
{
  psw1= 0;
  spx= 0;
  memset(rfile, 0, sizeof(rfile));
}


/* MCS-251 has 256 bytes of IRAM (edata region 00:0000-00:00FF), not the  */
/* mcs51 default of 128.  Override make_address_spaces to widen the IRAM  */
/* address space to 0x100 so the SPX stack can use the full low 256 bytes */
/* without falling off the end of the (128-entry) default IRAM view.      */
void
cl_uc251::make_address_spaces(void)
{
  rom= new cl_address_space("rom", 0, 0x10000, 8);
  rom->init();
  address_spaces->add(rom);

  iram= new cl_address_space("iram", 0, 0x100, 8);
  iram->init();
  address_spaces->add(iram);

  sfr= new cl_address_space("sfr", 0x80, 0x80, 8);
  sfr->init();
  address_spaces->add(sfr);

  xram= new cl_address_space("xram", 0, 0x10000, 8);
  xram->init();
  address_spaces->add(xram);

  regs= new cl_address_space("regs", 0, 8, 8);
  regs->init();
  address_spaces->add(regs);

  bits= new cl_address_space("bits", 0, 0x100, 1);
  bits->init();
  address_spaces->add(bits);

  dptr= new cl_address_space("dptr", 0, 4, 8);
  dptr->init();
  address_spaces->add(dptr);
}


/* MCS-251 register file helpers ---------------------------------------- */

/* Register file addressing:
 *   R0-R7   bank-selected -> IRAM[(bank*8) + n], bank = (PSW >> 3) & 3
 *   R8-R31  CPU register file (rfile[n-8]); R11 is an alias of ACC.
 * WR/DR tuples are big-endian and may span the bank/Rfile boundary.
 */
t_mem
cl_uc251::get_r8(int n)
{
  n &= 0x1f;
  if (n == 11)
    return(acc->read());
  if (n < 8)
    {
      int bank= (sfr->read(PSW) >> 3) & 3;
      return(iram->get_cell(bank * 8 + n)->read());
    }
  return(rfile[n - 8]);
}


void
cl_uc251::set_r8(int n, t_mem v)
{
  n &= 0x1f;
  if (n == 11)
    {
      acc->write(v);
      return;
    }
  if (n < 8)
    {
      int bank= (sfr->read(PSW) >> 3) & 3;
      iram->get_cell(bank * 8 + n)->write(v);
      return;
    }
  rfile[n - 8]= v & 0xff;
}


/* WRj = {Rj(MSB), Rj+1(LSB)} big-endian, j even */
t_mem
cl_uc251::get_wr(int j)
{
  j &= 0x1e;
  return((get_r8(j) << 8) | get_r8(j + 1));
}


void
cl_uc251::set_wr(int j, t_mem v)
{
  j &= 0x1e;
  set_r8(j, (v >> 8) & 0xff);
  set_r8(j + 1, v & 0xff);
}


/* DRk = {Rk(MSB) ... Rk+3(LSB)} big-endian, k multiple of 4.            */
/* Special aliases: DR56 = DPX = {DPXL(0x84), DPH, DPL} (24-bit),         */
/* DR60 = SPX (register-file number 15).  Per TSC80251/Intel 8XC251SB,    */
/* DPXL (SFR 0x84) holds bits 16-23 of DPX.  SDCC/mcs251 programs the    */
/* high byte via "mov dpxl,#hi", so DR56 must read/write DPXL, not the    */
/* legacy SFR 0x93.                                                       */
t_mem
cl_uc251::get_dr(int k)
{
  k &= 0x3c;
  if (k == 56) /* DPX = {DPXL, DPH, DPL} */
    return(((t_mem)sfr->read(0x84) << 16) | (sfr->read(DPH) << 8) | sfr->read(DPL));
  if (k == 60) /* SPX */
    return(spx);
  return(((t_mem)get_r8(k) << 24) | ((t_mem)get_r8(k + 1) << 16) |
	 ((t_mem)get_r8(k + 2) << 8) | get_r8(k + 3));
}


void
cl_uc251::set_dr(int k, t_mem v)
{
  k &= 0x3c;
  if (k == 56) /* DPX = {DPXL, DPH, DPL} */
    {
      sfr->write(DPL, v & 0xff);
      sfr->write(DPH, (v >> 8) & 0xff);
      sfr->write(0x84, (v >> 16) & 0xff);
      return;
    }
  if (k == 60) /* SPX */
    {
      spx= v & 0xffff;
      return;
    }
  set_r8(k, (v >> 24) & 0xff);
  set_r8(k + 1, (v >> 16) & 0xff);
  set_r8(k + 2, (v >> 8) & 0xff);
  set_r8(k + 3, v & 0xff);
}


/* Memory access helpers ------------------------------------------------ */

t_mem
cl_uc251::read_dir8(t_mem addr)
{
  if (addr < 0x80)
    return(iram->read(addr));
  return(sfr->read(addr));
}


void
cl_uc251::write_dir8(t_mem addr, t_mem v)
{
  if (addr < 0x80)
    iram->write(addr, v);
  else
    sfr->write(addr, v);
}


t_mem
cl_uc251::read_ri(int ri)
{
  return(iram->read(get_r8(ri)));
}


void
cl_uc251::write_ri(int ri, t_mem v)
{
  iram->write(get_r8(ri), v);
}


/* EDATA: 00:0000-00:00FF aliases IRAM; 00:0100-00:FFFF maps onto    */
/* xram[addr].  XDATA (0x010000+) maps onto xram[addr & 0xffff].     */
/* Region 00 also mirrors CODE/CONST in ROM: SDCC/mcs251 places code  */
/* in the low 64 KiB (region 00), overlapping the EDATA window.  Real  */
/* MCS-251 parts place code in region FF, but until the simulator's    */
/* ROM is extended to 24 bits we follow the 8051 von-Neumann convention*/
/* here: when a flat "@dpx" load hits a low address that holds code,   */
/* return the ROM byte so __gptrget can read CODE/CONST data.          */
t_mem
cl_uc251::read_edata(t_addr addr)
{
  addr &= 0xffffff;
  if (addr < 0x100)
    return(iram->read(addr));
  if (addr < 0x10000)
    {
      t_mem code= rom->read(addr);
      if (code != 0xff)                      /* ROM holds code/const here */
	return(code);
      return(xram->read(addr));
    }
  return(xram->read(addr & 0xffff));
}


void
cl_uc251::write_edata(t_addr addr, t_mem v)
{
  if (addr < 0x100)
    iram->write(addr, v);
  else
    xram->write(addr & 0xffff, v);
}


t_mem
cl_uc251::read_spx_dis16(t_addr dis)
{
  return(read_edata((spx + dis) & 0xffff));
}


void
cl_uc251::write_spx_dis16(t_addr dis, t_mem v)
{
  write_edata((spx + dis) & 0xffff, v);
}


/* CY/AC/OV per 8051 semantics (N/Z in PSW1) */
void
cl_uc251::set_flags_add8(t_mem a, t_mem b, t_mem r)
{
  t_mem newC= ((a + b) > 255) ? 0x80 : 0;
  t_mem newA= ((a & 0x0f) + (b & 0x0f)) & 0xf0;
  t_mem c6  = ((a & 0x7f) + (b & 0x7f)) & 0x80;
  bits->set(0xd7, newC);                        // CY
  SFR_SET_BIT(newC ^ c6, PSW, bmOV);
  SFR_SET_BIT(newA, PSW, bmAC);
  set_nz(r, 1);
}


void
cl_uc251::set_nz(t_mem r, int width)
{
  t_mem msb= (width >= 4) ? 0x80000000u : (width == 2) ? 0x8000 : 0x80;
  psw1= (r & msb) ? (psw1 | 0x80) : (psw1 & ~0x80);   /* N = sign bit */
  psw1= (r == 0) ? (psw1 | 0x40) : (psw1 & ~0x40);    /* Z */
}


int
cl_uc251::get_n(void)
{
  return((psw1 & 0x80) ? 1 : 0);
}


int
cl_uc251::get_z(void)
{
  return((psw1 & 0x40) ? 1 : 0);
}


/* Shared 8-bit ALU: A op= b (op 0=add, 1=addc, 2=subb, 3=anl, 4=orl, 5=xrl) */
int
cl_uc251::inst_alu_a_op8(int op, t_mem b)
{
  t_mem a= acc->read();
  t_mem r;
  switch (op)
    {
    case 0: r= a + b;               acc->write(r); set_flags_add8(a, b, r); break;
    case 1: {
      t_mem c= (bits->read(0xd7)) ? 1 : 0;
      r= a + b + c;
      acc->write(r);
      set_flags_add8(a, b + c, r);
      break;
    }
    case 2: {
      t_mem c= (bits->read(0xd7)) ? 1 : 0;
      r= a - b - c;
      acc->write(r);
      bits->set(0xd7, (a < (b + c)) ? 0x80 : 0);
      SFR_SET_BIT(((a ^ b ^ r) & 0x80) ? 1 : 0, PSW, bmOV);
      SFR_SET_BIT(((a & 0x0f) < ((b + c) & 0x0f)) ? 1 : 0, PSW, bmAC);
      break;
    }
    case 3: r= a & b;               acc->write(r); set_nz(r, 1); break;
    case 4: r= a | b;               acc->write(r); set_nz(r, 1); break;
    case 5: r= a ^ b;               acc->write(r); set_nz(r, 1); break;
    default: return(inst_unknown(op));
    }
  return(resGO);
}


int
cl_uc251::inst_alu_a_imm8(int op)
{
  return(inst_alu_a_op8(op, fetch()));
}


/* Stack helpers -------------------------------------------------------- */

int
cl_uc251::inst_ret251(void)
{
  t_mem h= read_edata(spx);                  // stack top = high byte (edata)
  spx= (spx - 1) & 0xffff;
  t_mem l= read_edata(spx);
  spx= (spx - 1) & 0xffff;
  PC= h * 256 + l;
  vc.rd+= 2;
  return(resGO);
}


int
cl_uc251::inst_reti251(void)
{
  return(inst_ret251());
}


/* ERET: pop 3 bytes (24-bit PC, little-endian push order -> top is MSB) */
int
cl_uc251::inst_eret251(void)
{
  t_mem h= read_edata(spx);
  spx= (spx - 1) & 0xffff;
  t_mem m= read_edata(spx);
  spx= (spx - 1) & 0xffff;
  t_mem l= read_edata(spx);
  spx= (spx - 1) & 0xffff;
  PC= (h << 16) | (m << 8) | l;
  vc.rd+= 3;
  return(resGO);
}


int
cl_uc251::inst_lcall16(t_mem addr)
{
  spx= (spx + 1) & 0xffff;
  write_edata(spx, PC & 0xff);              // push low byte
  spx= (spx + 1) & 0xffff;
  write_edata(spx, (PC >> 8) & 0xff);       // push high byte
  PC= addr;
  vc.wr+= 2;
  return(resGO);
}


int
cl_uc251::inst_ecall24(t_mem addr)
{
  spx= (spx + 1) & 0xffff;
  write_edata(spx, PC & 0xff);
  spx= (spx + 1) & 0xffff;
  write_edata(spx, (PC >> 8) & 0xff);
  spx= (spx + 1) & 0xffff;
  write_edata(spx, (PC >> 16) & 0xff);
  PC= addr;
  vc.wr+= 3;
  return(resGO);
}


/* A5-prefixed instructions (Source mode) -------------------------------- */
/* second byte: high 5 bits = function, low 3 bits = Rn or Ri (0/1)      */

int
cl_uc251::exec_a5(t_mem fnrn)
{
  int n= fnrn & 0x07;
  int ri= fnrn & 0x01;
  switch (fnrn >> 3)
    {
    case 0x01: /* INC @Ri */
      write_ri(ri, read_ri(ri) + 1);
      return(resGO);
    case 0x02: /* DEC @Ri */
      write_ri(ri, read_ri(ri) - 1);
      return(resGO);
    case 0x04: return(inst_alu_a_op8(0, read_ri(ri))); /* ADD A,@Ri */
    case 0x05: return(inst_alu_a_op8(0, get_r8(n)));    /* ADD A,Rn */
    case 0x06: return(inst_alu_a_op8(1, read_ri(ri))); /* ADDC A,@Ri */
    case 0x07: return(inst_alu_a_op8(1, get_r8(n)));    /* ADDC A,Rn */
    case 0x08: return(inst_alu_a_op8(4, read_ri(ri))); /* ORL A,@Ri */
    case 0x09: return(inst_alu_a_op8(4, get_r8(n)));    /* ORL A,Rn */
    case 0x0a: return(inst_alu_a_op8(3, read_ri(ri))); /* ANL A,@Ri */
    case 0x0b: return(inst_alu_a_op8(3, get_r8(n)));    /* ANL A,Rn */
    case 0x0c: return(inst_alu_a_op8(5, read_ri(ri))); /* XRL A,@Ri */
    case 0x0d: return(inst_alu_a_op8(5, get_r8(n)));    /* XRL A,Rn */
    case 0x0e: /* MOV @Ri,#data */
      write_ri(ri, fetch());
      return(resGO);
    case 0x17: /* CJNE Rn,#data,rel */
      {
	t_mem imm= fetch();
	t_mem rel= fetch();
	t_mem rv= get_r8(n);
	bits->set(0xd7, (rv < imm) ? 0x80 : 0);   /* CY = Rn < #data */
	if (rv != imm)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x1b: /* DJNZ Rn,rel */
      {
	t_mem rel= fetch();
	t_mem rv= get_r8(n) - 1;
	set_r8(n, rv);
	if (rv != 0)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x10: /* MOV dir8,@Ri */
      write_dir8(fetch(), read_ri(ri));
      return(resGO);
    case 0x12: return(inst_alu_a_op8(2, read_ri(ri))); /* SUBB A,@Ri */
    case 0x13: return(inst_alu_a_op8(2, get_r8(n)));    /* SUBB A,Rn */
    case 0x14: /* MOV @Ri,dir8 */
      write_ri(ri, read_dir8(fetch()));
      return(resGO);
    case 0x1c: /* MOV A,@Ri */
      acc->write(read_ri(ri));
      return(resGO);
    case 0x1d: /* MOV A,Rn */
      acc->write(get_r8(n));
      return(resGO);
    case 0x1e: /* MOV @Ri,A */
      write_ri(ri, acc->read());
      return(resGO);
    case 0x1f: /* MOV Rn,A */
      set_r8(n, acc->read());
      return(resGO);
    case 0x19: /* XCH A,Rn */
      {
	t_mem t= acc->read();
	acc->write(get_r8(n));
	set_r8(n, t);
	return(resGO);
      }
    default:
      return(inst_unknown(fnrn));
    }
}


/* 7E-prefixed MOV family.  Second byte: high nibble = register index    */
/* (Rm: direct 0-15; WRj: j/2; DRk: k/4), low nibble = operand type:     */
/*   0x0=#data8, 0x1=dir8, 0x4=#data16(WRj), 0x5=dir8(WRj)               */
/* (0x3 dir16, 0x7/0x8/0xc/0xd/0xf DRk forms: TODO)                      */

int
cl_uc251::exec_7e(t_mem sub)
{
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x00: /* MOV Rm,#data (Rn 0-7 subset) */
      set_r8(reg, fetch());
      return(resGO);
    case 0x01: /* MOV Rm,dir8 */
      set_r8(reg, read_dir8(fetch()));
      return(resGO);
    case 0x04: /* MOV WRj,#data16 */
      {
	t_mem h= fetch(), l= fetch();
	set_wr(reg * 2, (h << 8) | l);
	return(resGO);
      }
    case 0x05: /* MOV WRj,dir8 */
      set_wr(reg * 2, read_dir8(fetch()));
      return(resGO);
    case 0x09: /* MOV Rm,@WRj (third byte: dst<<4) */
      {
	t_mem dst= fetch();
	set_r8(dst >> 4, read_edata(get_wr(reg * 2)));
	return(resGO);
      }
    case 0x0b: /* MOV Rm,@DRk (high nibble k/4, 14=DPX, 15=SPX; third byte dst<<4) */
      {
	t_mem dst= fetch();
	t_mem v;
	if (reg == 15)
	  v= read_edata(spx);
	else if (reg == 14)
	  v= read_edata(get_dr(56));        /* @DPX */
	else
	  v= read_edata(get_dr(reg * 4));
	set_r8(dst >> 4, v);
	return(resGO);
      }
    case 0x08: /* MOV DRk,#0data16 (high 16 bits zero); SPX if reg==15 */
      {
	t_mem h= fetch(), l= fetch();
	if (reg == 15)
	  spx= (h << 8) | l;
	else
	  set_dr(reg * 4, (h << 8) | l);
	return(resGO);
      }
    case 0x0c: /* MOV DRk,#1data16 (high 16 bits ones); SPX if reg==15 */
      {
	t_mem h= fetch(), l= fetch();
	if (reg == 15)
	  spx= (h << 8) | l;
	else
	  set_dr(reg * 4, 0xffff0000 | (h << 8) | l);
	return(resGO);
      }
    case 0x0d: /* MOV DRk,dir8; SPX if reg==15 */
      if (reg == 15)
	spx= read_dir8(fetch());
      else
	set_dr(reg * 4, read_dir8(fetch()));
      return(resGO);
    default:
      return(inst_unknown(sub));
    }
}


/* 0B/1B-prefixed INC/DEC: second byte high nibble = register, low nibble =  */
/* family+step: Rm 0x0/0x1/0x2, WRj 0x4/0x5/0x6, DRk 0xc/0xd/0xe (15=SPX); */
/* step = 1<<v1 (#1/#2/#4)                                                  */

int
cl_uc251::exec_0b(t_mem sub, int dec)
{
  int reg= sub >> 4;
  int lo= sub & 0x0f;
  t_mem step;
  int kind;

  /* 0x0B/0x1B prefix also encodes 16-bit WRj memory moves when the low  */
  /* nibble is 8 (@WRj) or 0xA (@DRk): 0x0B = load WRj from memory,       */
  /* 0x1B = store WRj to memory.  Third byte = source/dest WRj index<<4. */
  if (lo == 0x08 || lo == 0x0a)
    {
      int wj= fetch() >> 4;                  /* WRj index (j = wj*2) */
      t_addr base;
      if (lo == 0x0a)
	base= get_dr(reg * 4);               /* @DRk */
      else
	base= get_wr(reg * 2);               /* @WRj */
      if (dec)                               /* 0x1B: store WRj (big-endian) */
	{
	  t_mem v= get_wr(wj * 2);
	  write_edata(base, (v >> 8) & 0xff);
	  write_edata(base + 1, v & 0xff);
	}
      else                                   /* 0x0B: load WRj (big-endian) */
	{
	  t_mem h= read_edata(base);
	  t_mem l= read_edata(base + 1);
	  set_wr(wj * 2, (h << 8) | l);
	}
      return(resGO);
    }

  if (lo < 3) { kind= 0; step= 1 << lo; }        /* Rm */
  else if (lo < 7) { kind= 1; step= 1 << (lo - 4); } /* WRj */
  else if (lo >= 0x0c && lo <= 0x0e) { kind= 2; step= 1 << (lo - 0x0c); } /* DRk */
  else return(inst_unknown(sub));
  if (kind == 2 && reg == 15)
    {
      spx= dec ? spx - step : spx + step;            /* SPX */
      set_nz(spx, 2);
    }
  else if (kind == 2)
    {
      t_mem r= dec ? get_dr(reg * 4) - step : get_dr(reg * 4) + step;
      set_dr(reg * 4, r);
      set_nz(r, 4);
    }
  else if (kind == 1)
    {
      t_mem r= dec ? get_wr(reg * 2) - step : get_wr(reg * 2) + step;
      set_wr(reg * 2, r);
      set_nz(r, 2);
    }
  else
    {
      t_mem r= dec ? get_r8(reg) - step : get_r8(reg) + step;
      set_r8(reg, r);
      set_nz(r, 1);
    }
  return(resGO);
}


/* Register-to-register moves: 7C Rm,Rm / 7D WRj,WRj / 7F DRk,DRk ------- */
/* second byte: high nibble = dest index, low nibble = source index      */

static int
exec_regmove(cl_uc251 *cpu, int code, t_mem sub)
{
  int d= sub >> 4, s= sub & 0x0f;
  switch (code)
    {
    case 0x7c: cpu->set_r8(d, cpu->get_r8(s)); break;          /* Rm */
    case 0x7d: cpu->set_wr(d * 2, cpu->get_wr(s * 2)); break;  /* WRj (j/2) */
    case 0x7f: cpu->set_dr(d * 4, cpu->get_dr(s * 4)); break;  /* DRk (k/4) */
    default: return(1);
    }
  return(0);
}


/* 7A-prefixed store forms: MOV dir8,Rm (low nibble 1) and               */
/* MOV @DRk/@SPX,Rm (low nibble 0x0b).  high nibble = dest register idx. */
static int
exec_7a(cl_uc251 *cpu, t_mem sub)
{
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x01: /* MOV dir8,Rm */
      cpu->write_dir8(cpu->fetch(), cpu->get_r8(reg));
      return(0);
    case 0x0b: /* MOV @DRk/@DPX/@SPX,Rm (third byte src<<4) */
      {
	t_mem src= cpu->fetch();
	t_mem v= cpu->get_r8(src >> 4);
	cpu->write_edata(cpu->get_dr(reg * 4), v);
	return(0);
      }
    default:
      return(1);
    }
}


/* Indexed MOV with 16-bit displacement (little-endian), SPX/DRk base:   */
/* 0x29 = 8-bit load, 0x69 = 16-bit load, 0x39 = 8-bit store,            */
/* 0x59 = 16-bit store.  second byte: (reg<<4)|indexed-reg (0x0f=SPX).   */
static int
exec_idx16(cl_uc251 *cpu, int code, t_mem sub)
{
  int reg= sub >> 4;
  int idx= sub & 0x0f;
  t_addr dis= (cpu->fetch() << 8) | cpu->fetch();  /* big-endian displacement */
  t_addr base= (idx == 15) ? cpu->spx : cpu->get_dr(idx * 4);
  t_addr a= (base + dis) & 0xffff;
  switch (code)
    {
    case 0x09: /* 8-bit load from @WRj+dis16 into Rm (high nibble) */
      cpu->set_r8(reg, cpu->read_edata(cpu->get_wr(idx * 2) + dis));
      return(0);
    case 0x29: /* 8-bit load into Rm (reg<<4) */
      cpu->set_r8(reg, cpu->read_edata(a));
      return(0);
    case 0x69: /* 16-bit load into WRj (reg<<4, j=reg*2) */
      cpu->set_wr(reg * 2, (cpu->read_edata(a) << 8) | cpu->read_edata(a + 1));
      return(0);
    case 0x39: /* 8-bit store from Rm (reg<<4) */
      cpu->write_edata(a, cpu->get_r8(reg));
      return(0);
    case 0x59: /* 16-bit store from WRj */
      cpu->write_edata(a, cpu->get_wr(reg * 2) >> 8);
      cpu->write_edata(a + 1, cpu->get_wr(reg * 2) & 0xff);
      return(0);
    case 0x79: /* 16-bit store from WRj (high nibble = src WRj) */
      cpu->write_edata(a, cpu->get_wr(reg * 2) >> 8);
      cpu->write_edata(a + 1, cpu->get_wr(reg * 2) & 0xff);
      return(0);
    default:
      return(1);
    }
}


/* CA-prefixed PUSH family: low nibble 2=#data8, 6=#data16, 8=Rm, 9=WRj,  */
/* 0xb=DRk; high nibble = register (Rm direct, WRj j/2, DRk k/4).         */
static int
exec_ca(cl_uc251 *cpu, t_mem sub)
{
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x02: /* PUSH #data8 */
      {
	t_mem v= cpu->fetch();
	cpu->spx= (cpu->spx + 1) & 0xffff;
	cpu->write_edata(cpu->spx, v);
	return(0);
      }
    case 0x06: /* PUSH #data16 */
      {
	t_mem h= cpu->fetch(), l= cpu->fetch();
	cpu->spx= (cpu->spx + 1) & 0xffff;
	cpu->write_edata(cpu->spx, l);
	cpu->spx= (cpu->spx + 1) & 0xffff;
	cpu->write_edata(cpu->spx, h);
	return(0);
      }
    case 0x08: /* PUSH Rm */
      {
	t_mem v= cpu->get_r8(reg);
	cpu->spx= (cpu->spx + 1) & 0xffff;
	cpu->write_edata(cpu->spx, v);
	return(0);
      }
    case 0x09: /* PUSH WRj */
      {
	t_mem v= cpu->get_wr(reg * 2);
	cpu->spx= (cpu->spx + 1) & 0xffff;
	cpu->write_edata(cpu->spx, v & 0xff);
	cpu->spx= (cpu->spx + 1) & 0xffff;
	cpu->write_edata(cpu->spx, (v >> 8) & 0xff);
	return(0);
      }
    case 0x0b: /* PUSH DRk */
      {
	t_mem v= cpu->get_dr(reg * 4);
	int i;
	for (i= 0; i < 4; i++)
	  {
	    cpu->spx= (cpu->spx + 1) & 0xffff;
	    cpu->write_edata(cpu->spx, (v >> (8 * i)) & 0xff);
	  }
	return(0);
      }
    default:
      return(1);
    }
}


/* DA-prefixed POP family: low nibble 8=Rm, 9=WRj, 0xb=DRk; high nibble   */
/* = register.  DR28 (reg 7) used for indirect call targets.              */
static int
exec_da(cl_uc251 *cpu, t_mem sub)
{
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x08: /* POP Rm */
      cpu->set_r8(reg, cpu->read_edata(cpu->spx));
      cpu->spx= (cpu->spx - 1) & 0xffff;
      return(0);
    case 0x09: /* POP WRj */
      {
	t_mem l= cpu->read_edata(cpu->spx);
	cpu->spx= (cpu->spx - 1) & 0xffff;
	t_mem h= cpu->read_edata(cpu->spx);
	cpu->spx= (cpu->spx - 1) & 0xffff;
	cpu->set_wr(reg * 2, (h << 8) | l);
	return(0);
      }
    case 0x0b: /* POP DRk (pop order: low byte first on stack top) */
      {
	t_mem v= 0;
	int i;
	for (i= 0; i < 4; i++)
	  {
	    v |= cpu->read_edata(cpu->spx) << (8 * i);
	    cpu->spx= (cpu->spx - 1) & 0xffff;
	  }
	cpu->set_dr(reg * 4, v);
	return(0);
      }
    default:
      return(1);
    }
}


/* 16/32-bit register ALU: second byte (d<<4)|(s<<4), reg encoding       */
/* WRj = nibble*2, DRk = nibble*4.  op codes (first byte):               */
/* add 0x2d/0x2f, sub 0x9d/0x9f, anl 0x5d/0x5f, orl 0x4d/0x4f,           */
/* xrl 0x6d/0x6f (WRj/DRk respectively).                                 */

/* Register-family ALU: prefixes 0x2e/0x4e/0x5e/0x6e/0x9e/0xbe           */
/* (ADD/ORL/ANL/XRL/SUB/CMP on Rm/WRj/DRk).  second byte like 7E:        */
/* high nibble = register, low nibble = operand type.  CMP sets flags.   */
static int
exec_alu_rm(cl_uc251 *cpu, int op, t_mem sub)
{
  int reg= sub >> 4;
  int width;                    /* 0=Rm(8), 1=WRj(16), 2=DRk(32) */
  t_mem src;
  switch (sub & 0x0f)
    {
    case 0x00: width= 0; src= cpu->fetch(); break;                 /* #data8 */
    case 0x01: width= 0; src= cpu->read_dir8(cpu->fetch()); break; /* dir8 */
    case 0x04: width= 1; src= (cpu->fetch() << 8) | cpu->fetch(); break; /* #data16 */
    case 0x05: width= 1; src= cpu->read_dir8(cpu->fetch()); break; /* dir8 */
    case 0x08: width= 2; src= (cpu->fetch() << 8) | cpu->fetch(); break; /* #0data16 */
    case 0x0c: width= 2; src= 0xffff0000u | (cpu->fetch() << 8) | cpu->fetch(); break; /* #1data16 */
    case 0x0d: width= 2; src= cpu->read_dir8(cpu->fetch()); break; /* dir8 */
    case 0x09: width= 0; src= cpu->read_edata(cpu->get_wr(reg * 2)); break; /* @WRj */
    case 0x0b: width= 0; src= cpu->read_edata(cpu->get_dr(reg * 4)); break; /* @DRk */
    default: return(1);
    }
  t_mem dst, r;
  if (width == 0)
    dst= cpu->get_r8(reg);
  else if (width == 1)
    dst= cpu->get_wr(reg * 2);
  else
    dst= cpu->get_dr(reg * 4);
  switch (op)
    {
    case 0: r= dst + src; break;       /* ADD */
    case 1: r= dst | src; break;       /* ORL */
    case 2: r= dst & src; break;       /* ANL */
    case 3: r= dst ^ src; break;       /* XRL */
    case 4: r= dst - src; break;       /* SUB */
    default: r= dst; break;            /* CMP: no write */
    }
  if (op != 5)
    {
      if (width == 0)
	cpu->set_r8(reg, r);
      else if (width == 1)
	cpu->set_wr(reg * 2, r);
      else
	cpu->set_dr(reg * 4, r);
    }
  cpu->set_nz(r, (width == 0) ? 1 : (width == 1) ? 2 : 4);
  /* flags: CY/OV/AC for add/sub/cmp (simplified for 16/32-bit) */
  if (op == 0 || op == 4 || op == 5)
    {
      t_mem mask= (width == 0) ? 0xff : (width == 1) ? 0xffff : 0xffffffffu;
      t_mem cy= (op == 4) ? (dst < src) : ((dst + src) > mask);
      if (op == 5)
	cy= (dst < src) || (dst == src) ? 0 : (dst > src) ? 0 : 0; /* CMP: CY=dst<src */
      cpu->bits->set(0xd7, cy ? 0x80 : 0);
      cpu->sfr->set(PSW, (cpu->sfr->get(PSW) & ~bmOV) |
		    ((((dst ^ src ^ r) & (mask >> 1) ^ 0) && (op != 2 && op != 3)) ? bmOV : 0));
    }
  return(0);
}

static int
exec_reg_alu(cl_uc251 *cpu, int code, t_mem sub)
{
  int d= sub >> 4, s= sub & 0x0f;
  int dr= (code & 0x02) != 0;   /* 0x2f/0x9f/0x5f/0x4f/0x6f -> DRk */
  int op;
  switch (code & 0xfd)
    {
    case 0x2d: op= 0; break;    /* add */
    case 0x9d: op= 1; break;    /* sub */
    case 0x5d: op= 2; break;    /* anl */
    case 0x4d: op= 3; break;    /* orl */
    case 0x6d: op= 4; break;    /* xrl */
    default: return(1);
    }
  if (dr)
    {
      t_mem a= cpu->get_dr(d * 4), b= cpu->get_dr(s * 4), r;
      switch (op)
	{
	case 0: r= a + b; break;
	case 1: r= a - b; break;
	case 2: r= a & b; break;
	case 3: r= a | b; break;
	default: r= a ^ b; break;
	}
      cpu->set_dr(d * 4, r);
      cpu->set_nz(r, 4);
      if (op < 2)
	{
	  cpu->bits->set(0xd7, (op == 0) ? ((a + b) > 0xffffffff) : (a < b));
	  cpu->sfr->set(PSW, cpu->sfr->get(PSW) & ~bmOV);
	}
    }
  else
    {
      t_mem a= cpu->get_wr(d * 2), b= cpu->get_wr(s * 2), r;
      switch (op)
	{
	case 0: r= a + b; break;
	case 1: r= a - b; break;
	case 2: r= a & b; break;
	case 3: r= a | b; break;
	default: r= a ^ b; break;
	}
      cpu->set_wr(d * 2, r);
      cpu->set_nz(r, 2);
      if (op < 2)
	{
	  t_mem cy= (op == 0) ? ((a + b) > 0xffff) : (a < b);
	  cpu->bits->set(0xd7, cy ? 0x80 : 0);
	  cpu->sfr->set(PSW, (cpu->sfr->get(PSW) & ~(bmOV|bmAC)) |
			(((a ^ b ^ r) & 0x8000) ? bmOV : 0));
	}
    }
  return(0);
}


/* Main decode/execute --------------------------------------------------- */

int
cl_uc251::exec_inst(void)
{
  /* Diagnostic: detect runaway into empty ROM (all 0xFF).  When the   */
  /* program jumps outside loaded code it executes 0xFF bytes, which    */
  /* look like valid instructions (MOV R7,A) and loop endlessly.  This  */
  /* checks a sliding window and stops early so the caller can see the  */
  /* jump target.  Enabled by UC251_STOP_ON_RUNAWAY.                    */
  if (getenv("UC251_STOP_ON_RUNAWAY"))
    {
      static t_addr last_good_pc= 0;
      static int runaway_state= 0;   /* 0=tracking, 1=runaway confirmed */
      t_addr p= PC;
      if (p <= 0x1000)
        last_good_pc= p;             /* still in code region */
      else if (p > 0x1000 && p < rom->get_size() && !runaway_state)
        {
          int i, empty= 1;
          for (i= 0; i < 8; i++)
            if (rom->read(p + i) != 0xff) { empty= 0; break; }
          if (empty)
            {
              runaway_state= 1;
              fprintf(stderr, "RUNAWAY: PC=0x%05x (last code PC=0x%05x)\n",
                      (unsigned)p, (unsigned)last_good_pc);
              return(resSTOP);
            }
        }
    }

  t_mem code= fetch();

  switch (code)
    {
    case 0x00: /* NOP */
      return(resGO);

    case 0x02: /* LJMP addr16 */
      {
	t_mem h= fetch(), l= fetch();
	PC= h * 256 + l;
	return(resGO);
      }
    case 0x12: /* LCALL addr16 */
      {
	t_mem h= fetch(), l= fetch();
	return(inst_lcall16(h * 256 + l));
      }
    case 0x22: /* RET */
      return(inst_ret251());
    case 0x32: /* RETI */
      return(inst_reti251());
    case 0xaa: /* ERET */
      return(inst_eret251());

    case 0x24: return(inst_alu_a_imm8(0));  /* ADD A,#data */
    case 0x34: return(inst_alu_a_imm8(1));  /* ADDC A,#data */
    case 0x94: return(inst_alu_a_imm8(2));  /* SUBB A,#data */
    case 0x54: return(inst_alu_a_imm8(3));  /* ANL A,#data */
    case 0x44: return(inst_alu_a_imm8(4));  /* ORL A,#data */
    case 0x64: return(inst_alu_a_imm8(5));  /* XRL A,#data */

    case 0x25: return(inst_alu_a_op8(0, read_dir8(fetch()))); /* ADD A,dir8 */
    case 0x35: return(inst_alu_a_op8(1, read_dir8(fetch()))); /* ADDC */
    case 0x95: return(inst_alu_a_op8(2, read_dir8(fetch()))); /* SUBB */
    case 0x55: return(inst_alu_a_op8(3, read_dir8(fetch()))); /* ANL */
    case 0x45: return(inst_alu_a_op8(4, read_dir8(fetch()))); /* ORL */
    case 0x65: return(inst_alu_a_op8(5, read_dir8(fetch()))); /* XRL */

    case 0x52: /* ANL dir8,A */
    case 0x42: /* ORL dir8,A */
    case 0x62: /* XRL dir8,A */
      {
	t_mem addr= fetch();
	t_mem d= read_dir8(addr);
	t_mem a= acc->read();
	write_dir8(addr, (code == 0x52) ? (d & a) : (code == 0x42) ? (d | a) : (d ^ a));
	return(resGO);
      }
    case 0x53: /* ANL dir8,#data */
    case 0x43: /* ORL dir8,#data */
    case 0x63: /* XRL dir8,#data */
      {
	t_mem addr= fetch();
	t_mem d= read_dir8(addr);
	t_mem imm= fetch();
	write_dir8(addr, (code == 0x53) ? (d & imm) : (code == 0x43) ? (d | imm) : (d ^ imm));
	return(resGO);
      }

    case 0x04: /* INC A */
      acc->write(acc->read() + 1);
      return(resGO);
    case 0x14: /* DEC A */
      acc->write(acc->read() - 1);
      return(resGO);
    case 0x03: /* RR A (rotate right, CY unchanged) */
      acc->write(((acc->read() >> 1) | (acc->read() << 7)) & 0xff);
      return(resGO);
    case 0x13: /* RRC A (rotate right through carry) */
      {
	t_mem a= acc->read();
	t_mem newCY= a & 1;
	acc->write((a >> 1) | ((bits->read(0xd7)) ? 0x80 : 0));
	bits->set(0xd7, newCY);
	return(resGO);
      }
    case 0x23: /* RL A (rotate left, CY unchanged) */
      acc->write(((acc->read() << 1) | (acc->read() >> 7)) & 0xff);
      return(resGO);
    case 0x33: /* RLC A (rotate left through carry) */
      {
	t_mem a= acc->read();
	t_mem newCY= (a >> 7) & 1;
	acc->write((a << 1) | ((bits->read(0xd7)) ? 1 : 0));
	bits->set(0xd7, newCY);
	return(resGO);
      }
    case 0xc4: /* SWAP A */
      acc->write(((acc->read() << 4) | (acc->read() >> 4)) & 0xff);
      return(resGO);
    case 0xc5: /* XCH A,dir8 */
      {
	t_mem addr= fetch();
	t_mem d= read_dir8(addr);
	write_dir8(addr, acc->read());
	acc->write(d);
	return(resGO);
      }
    case 0x05: /* INC dir8 */
    case 0x15: /* DEC dir8 */
      {
	t_mem addr= fetch();
	t_mem d= read_dir8(addr);
	write_dir8(addr, (code == 0x05) ? d + 1 : d - 1);
	return(resGO);
      }
    case 0xa3: /* INC DPTR */
      {
	t_mem dptr= (sfr->read(DPH) << 8) + sfr->read(DPL) + 1;
	sfr->write(DPL, dptr & 0xff);
	sfr->write(DPH, (dptr >> 8) & 0xff);
	return(resGO);
      }

    case 0xe4: /* CLR A */
      acc->write(0);
      return(resGO);
    case 0xf4: /* CPL A */
      acc->write(acc->read() ^ 0xff);
      return(resGO);
    case 0xc3: /* CLR CY */
      bits->set(0xd7, 0);
      return(resGO);
    case 0xd3: /* SETB CY */
      bits->set(0xd7, 1);
      return(resGO);
    case 0xd5: /* DJNZ dir8,rel */
      {
	t_mem addr= fetch();
	t_mem rel= fetch();
	t_mem v= read_dir8(addr) - 1;
	write_dir8(addr, v);
	if (v != 0)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0xb3: /* CPL CY */
      bits->set(0xd7, (bits->read(0xd7)) ? 0 : 1);
      return(resGO);

    case 0xb4: /* CJNE A,#data,rel */
      {
	t_mem imm= fetch();
	t_mem rel= fetch();
	t_mem a= acc->read();
	bits->set(0xd7, (a < imm) ? 0x80 : 0);    /* CY = A < #data */
	if (a != imm)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0xb5: /* CJNE A,dir8,rel */
      {
	t_mem d= read_dir8(fetch());
	t_mem rel= fetch();
	t_mem a= acc->read();
	bits->set(0xd7, (a < d) ? 0x80 : 0);      /* CY = A < dir8 */
	if (a != d)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }

    /* Bit operations on bit51 operands (classic 8051 bit addressing,      */
    /* shared opcode mapping with mcs51).  bit address 0x00-0x7F -> IRAM    */
    /* 0x20-0x2F, 0x80-0xFF -> bit-addressable SFRs; decoded by the         */
    /* inherited 'bits' address space.                                      */
    case 0xc2: /* CLR bit */
      bits->write(fetch(), 0);
      return(resGO);
    case 0xd2: /* SETB bit */
      bits->write(fetch(), 1);
      return(resGO);
    case 0xb2: /* CPL bit */
      {
	t_mem b= fetch();
	bits->write(b, bits->read(b) ? 0 : 1);
	return(resGO);
      }
    case 0x72: /* ORL C,bit */
      bits->write(0xd7, bits->read(0xd7) | bits->read(fetch()));
      return(resGO);
    case 0x82: /* ANL C,bit */
      bits->write(0xd7, bits->read(0xd7) & bits->read(fetch()));
      return(resGO);
    case 0xa0: /* ORL C,/bit */
      bits->write(0xd7, bits->read(0xd7) | (bits->read(fetch()) ? 0 : 1));
      return(resGO);
    case 0xb0: /* ANL C,/bit */
      bits->write(0xd7, bits->read(0xd7) & (bits->read(fetch()) ? 0 : 1));
      return(resGO);
    case 0xa2: /* MOV C,bit */
      bits->write(0xd7, bits->read(fetch()));
      return(resGO);
    case 0x92: /* MOV bit,C */
      bits->write(fetch(), bits->read(0xd7));
      return(resGO);
    case 0x10: /* JBC bit,rel (clear bit and jump if set) */
      {
	t_mem b= fetch();
	t_mem rel= fetch();
	int v= bits->read(b);
	if (v)
	  bits->write(b, 0);
	if (v)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x20: /* JB bit,rel */
      {
	t_mem b= fetch();
	t_mem rel= fetch();
	if (bits->read(b))
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x30: /* JNB bit,rel */
      {
	t_mem b= fetch();
	t_mem rel= fetch();
	if (!bits->read(b))
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }

    case 0x74: /* MOV A,#data */
      acc->write(fetch());
      return(resGO);

    case 0x75: /* MOV dir8,#data (dir8 0x00-0x7f -> IRAM, 0x80+ -> SFR) */
      {
	t_mem addr= fetch();
	t_mem data= fetch();
	write_dir8(addr, data);
	return(resGO);
      }
    case 0xe5: /* MOV A,dir8 */
      acc->write(read_dir8(fetch()));
      return(resGO);
    case 0xf5: /* MOV dir8,A */
      write_dir8(fetch(), acc->read());
      return(resGO);
    case 0x85: /* MOV dir8,dir8 */
      {
	t_mem src= fetch(), dst= fetch();
	write_dir8(dst, read_dir8(src));
	return(resGO);
      }

    case 0x90: /* MOV DPTR,#data16 */
      {
	t_mem h= fetch(), l= fetch();
	sfr->write(DPL, l);
	sfr->write(DPH, h);
	return(resGO);
      }

    case 0x60: /* JZ rel (simplified: ACC==0; 251 tests Z flag, TODO) */
    case 0x70: /* JNZ rel */
      {
	t_mem rel= fetch();
	if ((code == 0x60) == (acc->read() == 0))
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x40: /* JC rel */
    case 0x50: /* JNC rel */
      {
	t_mem rel= fetch();
	if ((code == 0x40) == ((bits->read(0xd7)) != 0))
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x08: /* JSLE rel (signed <=: N^V || Z) */
    case 0x18: /* JSG rel (signed >: !(N^V) && !Z) */
    case 0x28: /* JLE rel (signed <=) */
    case 0x38: /* JG rel (signed >) */
    case 0x48: /* JSL rel (signed <: N^V) */
    case 0x58: /* JSGE rel (signed >=: !(N^V)) */
    case 0x68: /* JE rel (Z) */
    case 0x78: /* JNE rel (!Z) */
      {
	t_mem rel= fetch();
	int n= get_n(), z= get_z();
	int v= (sfr->get(PSW) & bmOV) ? 1 : 0;
	int nv= n ^ v;
	int cond;
	switch (code)
	  {
	  case 0x08: case 0x28: cond= nv || z; break;          /* <= */
	  case 0x18: case 0x38: cond= !nv && !z; break;        /* > */
	  case 0x48: cond= nv; break;                          /* < */
	  case 0x58: cond= !nv; break;                         /* >= */
	  case 0x68: cond= z; break;                           /* == */
	  default:  cond= !z; break;                           /* != */
	  }
	if (cond)
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x80: /* SJMP rel */
      {
	t_mem rel= fetch();
	PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x73: /* JMP @A+DPTR */
      PC= (acc->read() + (sfr->read(DPH) << 8) + sfr->read(DPL));
      return(resGO);

    case 0x83: /* MOVC A,@A+PC */
      acc->write(rom->read((PC + acc->read()) & 0xffffff));
      return(resGO);
    case 0x93: /* MOVC A,@A+DPTR */
      acc->write(rom->read(((sfr->read(DPH) << 8) + sfr->read(DPL) + acc->read()) & 0xffffff));
      return(resGO);

    case 0x99: /* LCALL @WRj (low nibble 4) / ECALL @DRk (low nibble 8) */
      {
	t_mem sub= fetch();
	int reg= sub >> 4;
	if ((sub & 0x0f) == 4)      /* LCALL @WRj */
	  return(inst_lcall16(get_wr(reg * 2)));
	if ((sub & 0x0f) == 8)      /* ECALL @DRk */
	  return(inst_ecall24(get_dr(reg * 4) & 0xffffff));
	return(inst_unknown(0x99));
      }

    case 0x8a: /* EJMP addr24 */
      {
	t_mem b2= fetch(), b1= fetch(), b0= fetch();
	PC= (b2 << 16) | (b1 << 8) | b0;
	return(resGO);
      }
    case 0x9a: /* ECALL addr24 */
      {
	t_mem b2= fetch(), b1= fetch(), b0= fetch();
	return(inst_ecall24((b2 << 16) | (b1 << 8) | b0));
      }

    case 0xa5: /* A5-prefixed */
      return(exec_a5(fetch()));
    case 0x7e: /* 7E-prefixed */
      return(exec_7e(fetch()));
    case 0x7a: /* 7A-prefixed store forms */
      if (exec_7a(this, fetch()) == 0)
	return(resGO);
      return(inst_unknown(0x7a));
    case 0x09: /* MOV Rm,@WRj+dis16 (8-bit load) */
    case 0x29: /* MOV Rm,@idx+dis16 (8-bit load) */
    case 0x69: /* MOV WRj,@idx+dis16 (16-bit load) */
    case 0x39: /* MOV @idx+dis16,Rm (8-bit store) */
    case 0x59: /* MOV @idx+dis16,WRj (16-bit store) */
    case 0x79: /* MOV @idx+dis16,WRj (16-bit store, alt form) */
      if (exec_idx16(this, code, fetch()) == 0)
	return(resGO);
      return(inst_unknown(code));
    case 0x0b: /* INC family */
      return(exec_0b(fetch(), 0));
    case 0x1b: /* DEC family */
      return(exec_0b(fetch(), 1));

    case 0xa4: /* MUL AB: A*B -> B:A */
      {
	t_mem a= acc->read(), b= sfr->read(0xf0);       /* B at 0xF0 */
	t_mem p= a * b;
	acc->write(p & 0xff);
	sfr->write(0xf0, (p >> 8) & 0xff);
	bits->set(0xd7, 0);                       /* CY=0 */
	SFR_SET_BIT(p > 255, PSW, bmOV);
	return(resGO);
      }

    case 0xca: /* PUSH family */
      if (exec_ca(this, fetch()) == 0)
	return(resGO);
      return(inst_unknown(0xca));
    case 0xda: /* POP family */
      if (exec_da(this, fetch()) == 0)
	return(resGO);
      return(inst_unknown(0xda));

    case 0xc0: /* PUSH dir8 */
    case 0xd0: /* POP dir8 */
      {
	t_mem addr= fetch();
	if (code == 0xc0)
	  {
	    spx= (spx + 1) & 0xffff;
	    write_edata(spx, read_dir8(addr));
	  }
	else
	  {
	    write_dir8(addr, read_edata(spx));
	    spx= (spx - 1) & 0xffff;
	  }
	return(resGO);
      }

    case 0x2d: /* ADD WRj,WRj */
    case 0x9d: /* SUB WRj,WRj */
    case 0x5d: /* ANL WRj,WRj */
    case 0x4d: /* ORL WRj,WRj */
    case 0x6d: /* XRL WRj,WRj */
    case 0x2f: /* ADD DRk,DRk */
    case 0x9f: /* SUB DRk,DRk */
    case 0x5f: /* ANL DRk,DRk */
    case 0x4f: /* ORL DRk,DRk */
    case 0x6f: /* XRL DRk,DRk */
      if (exec_reg_alu(this, code, fetch()) == 0)
	return(resGO);
      return(inst_unknown(code));

    case 0x2e: /* ADD Rm/WRj/DRk,op */
    case 0x4e: /* ORL Rm/WRj/DRk,op */
    case 0x5e: /* ANL Rm/WRj/DRk,op */
    case 0x6e: /* XRL Rm/WRj/DRk,op */
    case 0x9e: /* SUB Rm/WRj/DRk,op */
    case 0xbe: /* CMP Rm/WRj/DRk,op */
      if (exec_alu_rm(this, (code == 0x2e) ? 0 : (code == 0x4e) ? 1 :
		      (code == 0x5e) ? 2 : (code == 0x6e) ? 3 :
		      (code == 0x9e) ? 4 : 5, fetch()) == 0)
	return(resGO);
      return(inst_unknown(code));

    case 0x7c: /* MOV Rm,Rm */
    case 0x7d: /* MOV WRj,WRj */
    case 0x7f: /* MOV DRk,DRk */
      if (exec_regmove(this, code, fetch()) == 0)
	return(resGO);
      return(inst_unknown(code));

    default:
      return(inst_unknown(code));
    }
}

/* End of s51.src/uc251.cc */
