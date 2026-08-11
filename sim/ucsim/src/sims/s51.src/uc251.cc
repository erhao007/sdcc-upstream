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
  memset(rom_loaded, 0, sizeof(rom_loaded));
  /* MCS-251 has a 24-bit program counter (16 MiB linear code space).  */
  PCmask= 0xffffff;
}


/* MCS-251 has 256 bytes of IRAM (edata region 00:0000-00:00FF), not the  */
/* mcs51 default of 128.  Override make_address_spaces to widen the IRAM  */
/* address space to 0x100 so the SPX stack can use the full low 256 bytes */
/* without falling off the end of the (128-entry) default IRAM view.      */
void
cl_uc251::make_address_spaces(void)
{
  rom= new cl_address_space("rom", 0, 0x20000, 8);
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

  /* EAXFR (extended SFR) region 0x7E0000-0x7EFFFF: I2C/PWM/DMA/CAN etc.,
     declared in stc32g12k128.h as __xdata __at(0x7E....) and reached via a
     24-bit @dpx pointer.  Has its own backing store so 0x7E.... no longer
     aliases xram[addr & 0xffff].  Chip + decoder are wired in make_chips()
     and decode_eaxfr() (see make_memories). */
  eaxfr= new cl_address_space("eaxfr", 0x7E0000, 0x10000, 8);
  eaxfr->init();
  address_spaces->add(eaxfr);

  /* XDATA region 0x010000-0x01FFFF: the real external SRAM (SDCC default
     xdata_loc).  Has its own backing store so region 01 no longer aliases
     the edata window (region 00) via xram[addr & 0xffff].  Chip + decoder
     are wired in make_chips() and decode_xdata() (see make_memories). */
  xdata= new cl_address_space("xdata", 0x010000, 0x10000, 8);
  xdata->init();
  address_spaces->add(xdata);

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


/* Chip + decoder for the EAXFR region (mirrors cl_51core::make_chips and  */
/* decode_xram).  make_memories orchestrates: address spaces (above),       */
/* chips, then decode_*; we chain to the inherited versions for everything  */
/* except eaxfr, then wire eaxfr<->eaxfr_chip ourselves.                    */
void
cl_uc251::make_chips(void)
{
  cl_uc51r::make_chips();   /* rom/iram/xram/sfr/eram chips (inherited) */

  eaxfr_chip= new cl_chip8("eaxfr_chip", 0x10000, 8);
  eaxfr_chip->init();
  memchips->add(eaxfr_chip);

  xdata_chip= new cl_chip8("xdata_chip", 0x10000, 8);
  xdata_chip->init();
  memchips->add(xdata_chip);
}

void
cl_uc251::make_memories(void)
{
  cl_uc89c51r::make_memories();   /* default address spaces + chips + decode_* */
  decode_eaxfr();
  decode_xdata();
}

void
cl_uc251::decode_eaxfr(void)
{
  class cl_address_decoder *ad;
  ad= new cl_address_decoder(eaxfr, eaxfr_chip, 0x7E0000, 0x7EFFFF, 0);
  ad->init();
  ad->set_name("def_eaxfr_decoder");
  eaxfr->decoders->add(ad);
  ad->activate(0);   /* clears CELL_NON_DECODED -> visible in info mem, VCD-safe */
}

void
cl_uc251::decode_xdata(void)
{
  class cl_address_decoder *ad;
  ad= new cl_address_decoder(xdata, xdata_chip, 0x010000, 0x01FFFF, 0);
  ad->init();
  ad->set_name("def_xdata_decoder");
  xdata->decoders->add(ad);
  ad->activate(0);   /* clears CELL_NON_DECODED -> visible in info mem, VCD-safe */
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


/* Record a ROM cell loaded from the hex file so read_edata's von-Neumann
   mirror can distinguish a genuine 0xFF data byte (loaded code/const)
   from an erased 0xFF (empty ROM).  Without this, crtxinit copying a
   float constant whose bytes include 0xFF fetches garbage from xram. */
bool
cl_uc251::set_rom(class cl_inspec *is, t_addr addr, t_mem val, bool check)
{
  bool r= cl_uc89c51r::set_rom(is, addr, val, check);
  if ((unsigned)t_addr(addr) < sizeof(rom_loaded) * 8)
    rom_loaded[addr >> 3] |= (1 << (addr & 7));
  return r;
}

bool
cl_uc251::rom_loaded_p(t_addr addr)
{
  if ((unsigned)addr >= sizeof(rom_loaded) * 8)
    return false;
  return (rom_loaded[addr >> 3] & (1 << (addr & 7))) != 0;
}


/* EDATA: 00:0000-00:00FF aliases IRAM; 00:0100-00:FFFF maps onto    */
/* xram[addr].  XDATA (0x010000+) maps onto xram[addr & 0xffff].     */
/* Region 00 also mirrors CODE/CONST in ROM: SDCC/mcs251 places code  */
/* in the low ROM (region 00), overlapping the EDATA window.  Real     */
/* MCS-251 parts place code in region FF, but until the simulator's    */
/* address-space model is switched over we follow the 8051 von-Neumann */
/* convention here: when a flat "@dpx" load hits a low address that    */
/* holds code, return the ROM byte so __gptrget can read CODE/CONST.   */
t_mem
cl_uc251::read_edata(t_addr addr)
{
  addr &= 0xffffff;
  if (addr < 0x100)
    return(iram->read(addr));
  if (addr < 0x10000)
    {
      /* Only treat the cell as code/const if the hex file actually
         loaded it; a bare 0xFF in erased ROM must fall through to xram. */
      if (rom_loaded_p(addr))
	return(rom->read(addr));
      return(xram->read(addr & 0xffff));
    }
  if (addr >= 0x010000 && addr < 0x020000)
    return(xdata->read(addr));   /* XDATA SRAM (de-aliased from the edata window) */
  if (addr >= 0x7E0000 && addr < 0x7F0000)
    return(eaxfr->read(addr));   /* extended SFR (I2C/PWM/DMA/CAN) */
  return(xram->read(addr & 0xffff));
}

/* Read the actual edata/xram/iram data cell, bypassing read_edata's
   von-Neumann ROM mirror.  The MCS-251 SPX hardware stack, PUSH/POP,
   LCALL/ECALL return frames and SPX-relative (@spx±dis) locals all live
   in real data RAM.  When the stack grows into 0x100..0xFFFF it overlaps
   the code region in the simulator's flat model; read_edata would then
   return the ROM byte and silently drop values that write_edata stored
   to xram (asymmetric read/write).  Use this for all genuine stack/data
   accesses so stores are visible to subsequent loads. */
t_mem
cl_uc251::read_edata_ram(t_addr addr)
{
  addr &= 0xffffff;
  if (addr < 0x100)
    return(iram->read(addr));
  return(xram->read(addr & 0xffff));
}


void
cl_uc251::write_edata(t_addr addr, t_mem v)
{
  addr &= 0xffffff;   /* match read_edata/read_edata_ram: a generic @dpx
                         pointer can carry a full 24-bit DPX (DPXL<<16 |
                         DPH<<8 | DPL), so normalise before the region
                         checks or a high-byte-set address would bypass
                         the EAXFR branch and fold into xram. */
  if (addr < 0x100)
    iram->write(addr, v);
  else if (addr >= 0x010000 && addr < 0x020000)
    xdata->write(addr, v);   /* XDATA SRAM (de-aliased from the edata window) */
  else if (addr >= 0x7E0000 && addr < 0x7F0000)
    eaxfr->write(addr, v);   /* extended SFR (I2C/PWM/DMA/CAN) */
  else
    xram->write(addr & 0xffff, v);
}


t_mem
cl_uc251::read_spx_dis16(t_addr dis)
{
  return(read_edata_ram((spx + dis) & 0xffff));
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
      t_mem c= (bits->get(0xd7)) ? 1 : 0;
      r= a + b + c;
      acc->write(r);
      set_flags_add8(a, b + c, r);
      break;
    }
    case 2: {
      t_mem c= (bits->get(0xd7)) ? 1 : 0;
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
  t_mem h= read_edata_ram(spx);             // stack top = high byte (edata)
  spx= (spx - 1) & 0xffff;
  t_mem l= read_edata_ram(spx);
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
  t_mem h= read_edata_ram(spx);
  spx= (spx - 1) & 0xffff;
  t_mem m= read_edata_ram(spx);
  spx= (spx - 1) & 0xffff;
  t_mem l= read_edata_ram(spx);
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
    case 0x00: /* fnrn 0x00-0x07 */
      /* INC @Ri is encoded A5 0x06/0x07 (fnrn >> 3 == 0).  The earlier code
         misfiled these under case 0x01 (which covers fnrn 0x08-0x0F), so the
         simulator silently dropped every 'inc @r0'/'inc @r1'. */
      if (fnrn >= 0x06)
        {
          /* INC @Ri */
          write_ri(ri, read_ri(ri) + 1);
          return(resGO);
        }
      /* 0x00-0x05: TODO other A5 sub-instructions */
      return(inst_unknown(fnrn));
    case 0x01: /* fnrn 0x08-0x0F: mixed group */
      /* 0x0A-0x0F: TODO other A5 sub-instructions */
      return(inst_unknown(fnrn));
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
    case 0x05: /* MOV WRj,dir8 (read 2 bytes: dir8, dir8+1, big-endian) */
      {
	t_mem addr= fetch();
	t_mem h= read_dir8(addr);
	t_mem l= read_dir8(addr + 1);
	set_wr(reg * 2, (h << 8) | l);
	return(resGO);
      }
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
	  v= read_edata_ram(spx);          /* @SPX = real stack RAM */
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
    case 0x0d: /* MOV DRk,dir8 (read 4 bytes big-endian; SPX reads 2) */
      {
	t_mem addr= fetch();
	if (reg == 15)
	  {
	    t_mem h= read_dir8(addr);
	    t_mem l= read_dir8(addr + 1);
	    spx= (h << 8) | l;
	  }
	else
	  {
	    t_mem b0= read_dir8(addr);
	    t_mem b1= read_dir8(addr + 1);
	    t_mem b2= read_dir8(addr + 2);
	    t_mem b3= read_dir8(addr + 3);
	    set_dr(reg * 4, ((t_mem)b0 << 24) | ((t_mem)b1 << 16) |
			  ((t_mem)b2 << 8) | b3);
	  }
	return(resGO);
      }
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
    case 0x05: /* MOV dir8,WRj (write 2 bytes big-endian) */
      {
	t_mem addr= cpu->fetch();
	t_mem v= cpu->get_wr(reg * 2);
	cpu->write_dir8(addr, (v >> 8) & 0xff);
	cpu->write_dir8(addr + 1, v & 0xff);
	return(0);
      }
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
      /* SPX-relative loads (@spx±dis) read the real stack RAM (no
         von-Neumann ROM mirror); DRk-based loads may be code/const
         pointers, keep the mirror. */
      cpu->set_r8(reg, (idx == 15) ? cpu->read_edata_ram(a) : cpu->read_edata(a));
      return(0);
    case 0x69: /* 16-bit load into WRj (reg<<4, j=reg*2) */
      if (idx == 15)
        cpu->set_wr(reg * 2, (cpu->read_edata_ram(a) << 8) | cpu->read_edata_ram(a + 1));
      else
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
      cpu->set_r8(reg, cpu->read_edata_ram(cpu->spx));
      cpu->spx= (cpu->spx - 1) & 0xffff;
      return(0);
    case 0x09: /* POP WRj */
      {
	t_mem l= cpu->read_edata_ram(cpu->spx);
	cpu->spx= (cpu->spx - 1) & 0xffff;
	t_mem h= cpu->read_edata_ram(cpu->spx);
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
	    v |= cpu->read_edata_ram(cpu->spx) << (8 * i);
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
    default: r= dst - src; break;      /* CMP: flags only, no write */
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
      t_mem cy= (op == 5) ? (dst < src) : (op == 4) ? (dst < src) : ((dst + src) > mask);
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


/* 0x2C/0x9C/0x5C/0x4C/0x6C: byte-wise register-register ALU ops        */
/* (ADD/SUB/ANL/ORL/XRL Rmd,Rms).  sub byte = (d<<4)|s where d,s are    */
/* Rm indices (R8-R31).  R11 is ACC.  Without these, SDCC codegen for   */
/* "__divulong" (which uses "add a,r15" to shift reste) silently fails. */
static int
exec_alu_rm_rm(cl_uc251 *cpu, int code, t_mem sub)
{
  int d= sub >> 4, s= sub & 0x0f;
  int op;
  switch (code)
    {
    case 0x2c: op= 0; break;    /* add */
    case 0x9c: op= 1; break;    /* sub */
    case 0x5c: op= 2; break;    /* anl */
    case 0x4c: op= 3; break;    /* orl */
    default: op= 4; break;      /* xrl */
    }
  t_mem a= cpu->get_r8(d), b= cpu->get_r8(s), r;
  switch (op)
    {
    case 0: r= a + b; break;
    case 1: r= a - b; break;
    case 2: r= a & b; break;
    case 3: r= a | b; break;
    default: r= a ^ b; break;
    }
  cpu->set_r8(d, r);
  cpu->set_nz(r, 1);
  if (op < 2)
    {
      t_mem cy= (op == 0) ? ((a + b) > 0xff) : (a < b);
      cpu->bits->set(0xd7, cy ? 0x80 : 0);
      cpu->sfr->set(PSW, (cpu->sfr->get(PSW) & ~(bmOV|bmAC)) |
		    (((a ^ b ^ r) & 0x80) ? bmOV : 0));
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

  /* Diagnostic: per-instruction trace to stderr.  Enabled by UC251_TRACE.
     UC251_TRACE=N traces only when PC is in [N, N+0x10000) (a code-range
     filter, hex).  Useful for debugging heisenbugs where source
     instrumentation changes codegen.  Format:
     PC SPX A B DPL DPH R4 R5 R6 R7  <disasm> */
  if (getenv("UC251_TRACE"))
    {
      static const char *tr_range= getenv("UC251_TRACE");
      unsigned long lo= 0, hi= 0xffffffff;
      int filtered= (tr_range[0] != '\0');
      if (filtered) { lo= strtoul(tr_range, 0, 16); hi= lo + 0x10000; }
      if (!filtered || ((unsigned long)PC >= lo && (unsigned long)PC < hi))
        {
          fprintf(stderr, "%05x spx=%04x a=%02x b=%02x dpl=%02x dph=%02x r4=%02x r5=%02x r6=%02x r7=%02x\n",
                  (unsigned)PC, (unsigned)spx,
                  acc->read(), sfr->read(0xf0),
                  sfr->read(0x82), sfr->read(0x83),
                  get_r8(4), get_r8(5), get_r8(6), get_r8(7));
        }
    }

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
	acc->write((a >> 1) | ((bits->get(0xd7)) ? 0x80 : 0));
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
	acc->write((a << 1) | ((bits->get(0xd7)) ? 1 : 0));
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
      bits->set(0xd7, (bits->get(0xd7)) ? 0 : 1);
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
      bits->write(0xd7, bits->get(0xd7) | bits->read(fetch()));
      return(resGO);
    case 0x82: /* ANL C,bit */
      bits->write(0xd7, bits->get(0xd7) & bits->read(fetch()));
      return(resGO);
    case 0xa0: /* ORL C,/bit */
      bits->write(0xd7, bits->get(0xd7) | (bits->read(fetch()) ? 0 : 1));
      return(resGO);
    case 0xb0: /* ANL C,/bit */
      bits->write(0xd7, bits->get(0xd7) & (bits->read(fetch()) ? 0 : 1));
      return(resGO);
    case 0xa2: /* MOV C,bit */
      bits->write(0xd7, bits->read(fetch()));
      return(resGO);
    case 0x92: /* MOV bit,C */
      bits->write(fetch(), bits->get(0xd7));
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
	if ((code == 0x40) == ((bits->get(0xd7)) != 0))
	  PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x0a: /* MOVZ WRj,Rm (zero-extend; second byte (dst<<4)|src) */
      {
	t_mem b= fetch();
	int dst= b >> 4, src= b & 0x0f;
	set_wr(dst * 2, get_r8(src));
	return(resGO);
      }
    case 0x1a: /* MOVS WRj,Rm (sign-extend; second byte (dst<<4)|src) */
      {
	t_mem b= fetch();
	int dst= b >> 4, src= b & 0x0f;
	t_mem v= get_r8(src);
	if (v & 0x80)
	  v|= 0xff00;
	set_wr(dst * 2, v);
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

    case 0x89: /* EJMP @DRk (low nibble 8) — indirect jump through a DRk */
      {
	t_mem sub= fetch();
	if ((sub & 0x0f) == 8)
	  {
	    int reg= sub >> 4;
	    PC= get_dr(reg * 4) & 0xffffff;
	    return(resGO);
	  }
	return(inst_unknown(0x89));
      }

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
    case 0xad: /* MUL WRj,WRms: 16x16 -> 32-bit in DRk (WRj:WRj+2, big-endian) */
      {
	t_mem sub= fetch();
	int d= sub >> 4, s= sub & 0x0f;
	unsigned int a= get_wr(d * 2), b= get_wr(s * 2);
	unsigned long p= (unsigned long)a * (unsigned long)b;
	set_dr(d * 2, p);                        /* DRk: j=d → DR(d/2), byte index d*2 */
	bits->set(0xd7, 0);                       /* CY=0 */
	set_nz(p, 4);
	return(resGO);
      }
    case 0x84: /* DIV AB: A/B -> A=quotient, B=remainder */
      {
	t_mem a= acc->read(), b= sfr->read(0xf0);
	t_mem psw= sfr->get(PSW);
	bits->set(0xd7, 0);                       /* CY=0 */
	if (b == 0)
	  psw|= bmOV;                             /* OV=1 divide-by-zero */
	else
	  {
	    psw&= ~bmOV;
	    acc->write(a / b);
	    sfr->write(0xf0, a % b);
	  }
	sfr->set(PSW, psw);
	return(resGO);
      }
    case 0x8d: /* DIV WRj,WRms: 16/16 -> quotient in WRj, remainder in WRj+2 */
      {
	t_mem sub= fetch();
	int d= sub >> 4, s= sub & 0x0f;
	unsigned int a= get_wr(d * 2), b= get_wr(s * 2);
	bits->set(0xd7, 0);                       /* CY=0 */
	if (b == 0)
	  sfr->set(PSW, sfr->get(PSW) | bmOV);   /* OV=1 divide-by-zero */
	else
	  {
	    sfr->set(PSW, sfr->get(PSW) & ~bmOV);
	    set_wr(d * 2, a / b);                 /* quotient in WRj */
	    set_wr(d * 2 + 2, a % b);             /* remainder in WRj+2 */
	  }
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
	    /* POP dir8 reads the real stack RAM (no von-Neumann ROM mirror),
	       matching PUSH dir8's write_edata.  read_edata's mirror would
	       return the overlapping ROM byte for stack addresses in
	       0x100..0xFFFF, dropping the saved register value. */
	    write_dir8(addr, read_edata_ram(spx));
	    spx= (spx - 1) & 0xffff;
	  }
	return(resGO);
      }

    case 0xe0: /* MOVX A,@DPTR */
      acc->write(xram->read((sfr->read(DPH) << 8) | sfr->read(DPL)));
      return(resGO);
    case 0xe2: /* MOVX A,@R0 */
      acc->write(xram->read(get_r8(0)));
      return(resGO);
    case 0xe3: /* MOVX A,@R1 */
      acc->write(xram->read(get_r8(1)));
      return(resGO);
    case 0xf0: /* MOVX @DPTR,A */
      xram->write((sfr->read(DPH) << 8) | sfr->read(DPL), acc->read());
      return(resGO);
    case 0xf2: /* MOVX @R0,A */
      xram->write(get_r8(0), acc->read());
      return(resGO);
    case 0xf3: /* MOVX @R1,A */
      xram->write(get_r8(1), acc->read());
      return(resGO);

    case 0x2c: /* ADD Rmd,Rms */
    case 0x9c: /* SUB Rmd,Rms */
    case 0x5c: /* ANL Rmd,Rms */
    case 0x4c: /* ORL Rmd,Rms */
    case 0x6c: /* XRL Rmd,Rms */
      if (exec_alu_rm_rm(this, code, fetch()) == 0)
	return(resGO);
      return(inst_unknown(code));

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

    case 0xbc: /* CMP Rm,Rm (byte; A is the usual destination).  SDCC
		  emits this for 16/32-bit equality compares (cmp a,Rn),
		  encoding the pair as 0xBC (dst << 4 | src). */
      {
	t_mem sub= fetch();
	t_mem dst= get_r8(sub >> 4);
	t_mem src= get_r8(sub & 0x0f);
	t_mem r= dst - src;                 /* compare: flags only, no write */
	set_nz(r, 1);
	bits->set(0xd7, (dst < src) ? 0x80 : 0);   /* CY = dst < src */
	sfr->set(PSW, (sfr->get(PSW) & ~bmOV) |
		      ((((dst ^ src ^ r) & 0x7f)) ? bmOV : 0));
	return(resGO);
      }

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


/* ====================================================================== */
/* Disassembly (MCS-251 Source mode)                                       */
/*                                                                        */
/* cl_uc251 inherits cl_51core's 8051 table disassembler, which splits    */
/* every MCS-251 prefix byte (A5/7E/7A/7C-7F/0B-1B/09/29/.../89/99/BC/   */
/* CA/DA) into unrelated 8051 ops.  disass_251 mirrors exec_inst's        */
/* dispatch tree to decode each implemented MCS-251 instruction as one    */
/* line.  Operand bytes are read from ROM at fixed offsets (no fetch /    */
/* PC advance); the mnemonic is formatted with the same 6-column padding   */
/* convention as cl_51core::disass (mnemonic padded to width 6, then one   */
/* separating space, then operands).  Unknown/illegal bytes return         */
/* length 1 and emit ".db 0xNN" so the dc loop always advances.            */
/* ====================================================================== */

/* Pad the mnemonic field already written to *out to 6 columns, then append
   the single separating space before operands.  No-op when out==NULL. */
static void
mne_sep(chars *out)
{
  if (!out)
    return;
  while (out->len() < 6)
    out->append(' ');
  out->append(' ');
}

/* daddr_name formats its buffer with chars::format (replace), which would
   wipe the mnemonic already written to the work buffer.  Route through a
   private temp and append the result.  (baddr_name uses appendf and is safe
   to call directly on the work buffer.) */
static void
dir8_out(cl_uc251 *cpu, t_mem a, chars *out)
{
  if (!out)
    return;
  chars tmp;
  cpu->daddr_name(a, &tmp);
  out->append(tmp.c_str());
}

void
cl_uc251::rname(int idx, chars *out)
{
  if (out)
    out->appendf("R%d", idx);
}

void
cl_uc251::wrname(int reg, chars *out)
{
  if (out)
    out->appendf("WR%d", reg * 2);
}

void
cl_uc251::drname(int reg, chars *out)
{
  if (out)
    out->appendf("DR%d", reg * 4);
}

void
cl_uc251::riname(int ri, chars *out)
{
  if (out)
    out->appendf("@R%d", ri & 1);
}

char *
cl_uc251::disass(t_addr addr)
{
  chars work;
  (void)disass_251(addr, &work);
  return strdup(work.c_str());
}

int
cl_uc251::inst_length(t_addr addr)
{
  return disass_251(addr, NULL);
}

int
cl_uc251::longest_inst(void)
{
  return 4;     /* EJMP/ECALL addr24 are 4 bytes */
}


/* --- per-family sub-decoders ------------------------------------------ */

/* A5 <fnrn>: fnrn = (function<<3) | (Rn or @Ri selector).  Mirrors exec_a5. */
int
cl_uc251::disass_a5(t_addr addr, chars *out)
{
  t_mem fnrn= rom->get(addr + 1);
  int n= fnrn & 0x07;
  int ri= fnrn & 0x01;
  const char *opname= NULL;     /* binary "A,<rhs>" ALU op (op2 in exec_a5) */
  int len= 2;                   /* A5 + fnrn by default */
  const char *form= NULL;       /* non-ALU mnemonic form, fully custom */

  switch (fnrn >> 3)
    {
    case 0x00:
      if (fnrn >= 0x06) form= "INC";        /* INC @Ri */
      break;
    case 0x02: form= "DEC"; break;          /* DEC @Ri */
    case 0x04: opname= "ADD"; break;        /* ADD A,@Ri */
    case 0x05: opname= "ADD"; break;        /* ADD A,Rn  */
    case 0x06: opname= "ADDC"; break;
    case 0x07: opname= "ADDC"; break;
    case 0x08: opname= "ORL"; break;
    case 0x09: opname= "ORL"; break;
    case 0x0a: opname= "ANL"; break;
    case 0x0b: opname= "ANL"; break;
    case 0x0c: opname= "XRL"; break;
    case 0x0d: opname= "XRL"; break;
    case 0x12: opname= "SUBB"; break;
    case 0x13: opname= "SUBB"; break;
    case 0x19: form= "XCH"; break;          /* XCH A,Rn */
    case 0x1c: form= "MOV"; break;          /* MOV A,@Ri (src=@Ri) */
    case 0x1d: form= "MOVRN"; break;        /* MOV A,Rn */
    case 0x1e: form= "MOVRI"; break;        /* MOV @Ri,A */
    case 0x1f: form= "MOVRNA"; break;       /* MOV Rn,A */
    case 0x0e:                               /* MOV @Ri,#data */
      len= 3;
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          riname(ri, out); out->append(",#");
          out->appendf("0x%02x", (unsigned)rom->get(addr + 2));
        }
      return len;
    case 0x10:                               /* MOV dir8,@Ri */
      len= 3;
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          dir8_out(this, rom->get(addr + 2), out); out->append(",");
          riname(ri, out);
        }
      return len;
    case 0x14:                               /* MOV @Ri,dir8 */
      len= 3;
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          riname(ri, out); out->append(",");
          dir8_out(this, rom->get(addr + 2), out);
        }
      return len;
    case 0x17:                               /* CJNE Rn,#data,rel */
      len= 4;
      if (out)
        {
          t_addr target= (addr + len + (i32_t)(i8_t)rom->get(addr + 3)) & 0xffffff;
          out->append("CJNE"); mne_sep(out);
          rname(n, out); out->append(",#");
          out->appendf("0x%02x,", (unsigned)rom->get(addr + 2));
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return len;
    case 0x1b:                               /* DJNZ Rn,rel */
      len= 3;
      if (out)
        {
          t_addr target= (addr + len + (i32_t)(i8_t)rom->get(addr + 2)) & 0xffffff;
          out->append("DJNZ"); mne_sep(out);
          rname(n, out); out->append(",");
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return len;
    default: break;   /* unimplemented A5 function */
    }

  if (opname)
    {
      /* ADD/ADDC/ORL/ANL/XRL/SUBB A,{@Ri|Rn} */
      if (out)
        {
          out->append(opname); mne_sep(out);
          out->append("A,");
          if (((fnrn >> 3) & 1) == 0)
            riname(ri, out);          /* even function codes: @Ri form */
          else
            rname(n, out);            /* odd function codes: Rn form */
        }
      return 2;
    }
  if (form)
    {
      if (out)
        {
          /* All the @Ri/Rn MOV/XCH/INC/DEC single-byte-operand forms. */
          if (strcmp(form, "INC") == 0 || strcmp(form, "DEC") == 0)
            {
              out->append(form); mne_sep(out);
              riname(ri, out);
            }
          else if (strcmp(form, "XCH") == 0)        /* XCH A,Rn */
            {
              out->append("XCH"); mne_sep(out);
              out->append("A,"); rname(n, out);
            }
          else if (strcmp(form, "MOV") == 0)        /* MOV A,@Ri */
            {
              out->append("MOV"); mne_sep(out);
              out->append("A,"); riname(ri, out);
            }
          else if (strcmp(form, "MOVRN") == 0)      /* MOV A,Rn */
            {
              out->append("MOV"); mne_sep(out);
              out->append("A,"); rname(n, out);
            }
          else if (strcmp(form, "MOVRI") == 0)      /* MOV @Ri,A */
            {
              out->append("MOV"); mne_sep(out);
              riname(ri, out); out->append(",A");
            }
          else                                      /* MOVRNA: MOV Rn,A */
            {
              out->append("MOV"); mne_sep(out);
              rname(n, out); out->append(",A");
            }
        }
      return 2;
    }
  /* Unknown A5 function: emit the prefix byte as data and let dc move on. */
  if (out)
    out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
  return 1;
}


/* 7E <reg><type>: MOV Rm/WRj/DRk immediate/direct family.  Mirrors exec_7e. */
int
cl_uc251::disass_7e(t_addr addr, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x00:        /* MOV Rm,#data */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          rname(reg, out); out->append(",#");
          out->appendf("0x%02x", (unsigned)rom->get(addr + 2));
        }
      return 3;
    case 0x01:        /* MOV Rm,dir8 */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          rname(reg, out); out->append(",");
          dir8_out(this, rom->get(addr + 2), out);
        }
      return 3;
    case 0x04:        /* MOV WRj,#data16 */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          wrname(reg, out); out->append(",#");
          out->appendf("0x%04x",
                       (unsigned)(rom->get(addr + 2) * 256 + rom->get(addr + 3)));
        }
      return 4;
    case 0x05:        /* MOV WRj,dir8 (read dir8,dir8+1) */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          wrname(reg, out); out->append(",");
          dir8_out(this, rom->get(addr + 2), out);
        }
      return 3;
    case 0x08:        /* MOV DRk,#0data16 (high 16 bits zero) */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          drname(reg, out); out->append(",#0x");
          out->appendf("%04x",
                       (unsigned)(rom->get(addr + 2) * 256 + rom->get(addr + 3)));
        }
      return 4;
    case 0x0c:        /* MOV DRk,#1data16 (high 16 bits ones) */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          drname(reg, out); out->append(",#0xffff");
          out->appendf("%04x",
                       (unsigned)(rom->get(addr + 2) * 256 + rom->get(addr + 3)));
        }
      return 4;
    case 0x0d:        /* MOV DRk,dir8 (read 4 bytes big-endian) */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          drname(reg, out); out->append(",");
          dir8_out(this, rom->get(addr + 2), out);
        }
      return 3;
    case 0x09:        /* MOV Rm,@WRj (third byte: dst<<4) */
      if (out)
        {
          int dst= rom->get(addr + 2) >> 4;
          out->append("MOV"); mne_sep(out);
          rname(dst, out); out->append(",@");
          wrname(reg, out);
        }
      return 3;
    case 0x0b:        /* MOV Rm,@DRk/@DPX/@SPX (third byte: dst<<4) */
      if (out)
        {
          int dst= rom->get(addr + 2) >> 4;
          out->append("MOV"); mne_sep(out);
          rname(dst, out); out->append(",@");
          drname(reg, out);
        }
      return 3;
    default: break;   /* unimplemented 7E type */
    }
  if (out)
    out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
  return 1;
}


/* 7A <reg><type>: store forms.  Mirrors exec_7a. */
int
cl_uc251::disass_7a(t_addr addr, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x01:        /* MOV dir8,Rm */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          dir8_out(this, rom->get(addr + 2), out); out->append(",");
          rname(reg, out);
        }
      return 3;
    case 0x05:        /* MOV dir8,WRj */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          dir8_out(this, rom->get(addr + 2), out); out->append(",");
          wrname(reg, out);
        }
      return 3;
    case 0x0b:        /* MOV @DRk/@SPX,Rm (third byte: src<<4) */
      if (out)
        {
          int src= rom->get(addr + 2) >> 4;
          out->append("MOV"); mne_sep(out);
          out->append("@"); drname(reg, out); out->append(",");
          rname(src, out);
        }
      return 3;
    default: break;
    }
  if (out)
    out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
  return 1;
}


/* 7C/7D/7F <d><s>: register-to-register moves.  Mirrors exec_regmove. */
int
cl_uc251::disass_regmove(t_addr addr, int code, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int d= sub >> 4, s= sub & 0x0f;
  if (out)
    {
      out->append("MOV"); mne_sep(out);
      switch (code)
        {
        case 0x7c: rname(d, out); out->append(","); rname(s, out); break;
        case 0x7d: wrname(d, out); out->append(","); wrname(s, out); break;
        default:   drname(d, out); out->append(","); drname(s, out); break;  /* 0x7f */
        }
    }
  return 2;
}


/* 0B/1B prefix: INC/DEC family + embedded 16-bit WRj memory move.
   Mirrors exec_0b.  dec=0 for 0x0B, dec=1 for 0x1B. */
int
cl_uc251::disass_0b(t_addr addr, int dec, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  int lo= sub & 0x0f;
  const char *mne= dec ? "DEC" : "INC";

  if (lo == 0x08 || lo == 0x0a)
    {
      /* 16-bit WRj memory move; third byte = src/dst WRj index << 4. */
      int wj= rom->get(addr + 2) >> 4;
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          if (dec)      /* 0x1B: store WRj -> @base */
            {
              out->append("@");
              if (lo == 0x0a) drname(reg, out);
              else            wrname(reg, out);
              out->append(",");
              wrname(wj, out);
            }
          else          /* 0x0B: load WRj <- @base */
            {
              wrname(wj, out); out->append(",@");
              if (lo == 0x0a) drname(reg, out);
              else            wrname(reg, out);
            }
        }
      return 3;
    }

  int kind;    /* 0=Rm, 1=WRj, 2=DRk */
  if (lo < 3)             kind= 0;
  else if (lo >= 4 && lo <= 6) kind= 1;
  else if (lo >= 0x0c && lo <= 0x0e) kind= 2;
  else
    {
      if (out)
        out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
      return 1;
    }
  if (out)
    {
      out->append(mne); mne_sep(out);
      if (kind == 0)      rname(reg, out);
      else if (kind == 1) wrname(reg, out);
      else                drname(reg, out);   /* reg 15 -> DR60 (== SPX alias) */
    }
  return 2;
}


/* 09/29/39/59/69/79: indexed MOV with 16-bit displacement.  Mirrors
   exec_idx16.  Encoding: sub=(reg<<4)|idx (idx 15 = SPX base for DRk forms);
   then 2 big-endian displacement bytes.  Note: 0x09 uses WRj base
   (WR(idx*2)); the others use DRk base (DR(idx*4)) or SPX if idx==15. */
int
cl_uc251::disass_idx16(t_addr addr, int code, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  int idx= sub & 0x0f;
  unsigned dis= (unsigned)(rom->get(addr + 2) * 256 + rom->get(addr + 3));
  if (out)
    {
      out->append("MOV"); mne_sep(out);
      /* base register name for the DRk forms (idx 15 -> SPX) */
      bool spx= (idx == 15);
      switch (code)
        {
        case 0x09:       /* MOV Rm,@WRj+dis16 (WRj base) */
          rname(reg, out); out->append(",@");
          wrname(idx, out); out->appendf("+0x%04x", dis);
          break;
        case 0x29:       /* MOV Rm,@DRk/SPX+dis16 */
          rname(reg, out); out->append(",@");
          if (spx) out->append("SPX"); else drname(idx, out);
          out->appendf("+0x%04x", dis);
          break;
        case 0x69:       /* MOV WRj,@DRk/SPX+dis16 */
          wrname(reg, out); out->append(",@");
          if (spx) out->append("SPX"); else drname(idx, out);
          out->appendf("+0x%04x", dis);
          break;
        case 0x39:       /* MOV @DRk/SPX+dis16,Rm */
          out->append("@");
          if (spx) out->append("SPX"); else drname(idx, out);
          out->appendf("+0x%04x,", dis);
          rname(reg, out);
          break;
        default:         /* 0x59 / 0x79: MOV @DRk/SPX+dis16,WRj */
          out->append("@");
          if (spx) out->append("SPX"); else drname(idx, out);
          out->appendf("+0x%04x,", dis);
          wrname(reg, out);
          break;
        }
    }
  return 4;
}


/* CA <reg><type>: PUSH family.  Mirrors exec_ca. */
int
cl_uc251::disass_ca(t_addr addr, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x02:      /* PUSH #data8 */
      if (out)
        {
          out->append("PUSH"); mne_sep(out);
          out->appendf("#0x%02x", (unsigned)rom->get(addr + 2));
        }
      return 3;
    case 0x06:      /* PUSH #data16 (big-endian; pushed low-then-high) */
      if (out)
        {
          out->append("PUSH"); mne_sep(out);
          out->appendf("#0x%04x",
                       (unsigned)(rom->get(addr + 2) * 256 + rom->get(addr + 3)));
        }
      return 4;
    case 0x08:      /* PUSH Rm */
      if (out) { out->append("PUSH"); mne_sep(out); rname(reg, out); }
      return 2;
    case 0x09:      /* PUSH WRj */
      if (out) { out->append("PUSH"); mne_sep(out); wrname(reg, out); }
      return 2;
    case 0x0b:      /* PUSH DRk */
      if (out) { out->append("PUSH"); mne_sep(out); drname(reg, out); }
      return 2;
    default: break;
    }
  if (out)
    out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
  return 1;
}


/* DA <reg><type>: POP family.  Mirrors exec_da.  Always 2 bytes. */
int
cl_uc251::disass_da(t_addr addr, chars *out)
{
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  switch (sub & 0x0f)
    {
    case 0x08:
      if (out) { out->append("POP"); mne_sep(out); rname(reg, out); }
      return 2;
    case 0x09:
      if (out) { out->append("POP"); mne_sep(out); wrname(reg, out); }
      return 2;
    case 0x0b:
      if (out) { out->append("POP"); mne_sep(out); drname(reg, out); }
      return 2;
    default: break;
    }
  if (out)
    out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
  return 1;
}


/* 2E/4E/5E/6E/9E/BE: ADD/ORL/ANL/XRL/SUB/CMP Rm/WRj/DRk,<operand>.
   Mirrors exec_alu_rm.  op: 0=ADD,1=ORL,2=ANL,3=XRL,4=SUB,5=CMP. */
int
cl_uc251::disass_alu_rm(t_addr addr, int op, chars *out)
{
  static const char * const name[6]= {"ADD","ORL","ANL","XRL","SUB","CMP"};
  t_mem sub= rom->get(addr + 1);
  int reg= sub >> 4;
  int width;     /* 0=Rm, 1=WRj, 2=DRk */
  int len;
  /* compute length + width from the low nibble, matching exec_alu_rm */
  switch (sub & 0x0f)
    {
    case 0x00: width= 0; len= 3; break;   /* #data8  */
    case 0x01: width= 0; len= 3; break;   /* dir8    */
    case 0x04: width= 1; len= 4; break;   /* #data16 */
    case 0x05: width= 1; len= 3; break;   /* dir8    */
    case 0x08: width= 2; len= 4; break;   /* #0data16*/
    case 0x0c: width= 2; len= 4; break;   /* #1data16*/
    case 0x0d: width= 2; len= 3; break;   /* dir8    */
    case 0x09: width= 0; len= 2; break;   /* @WRj    */
    case 0x0b: width= 0; len= 2; break;   /* @DRk    */
    default:
      if (out)
        out->appendf(".db 0x%02x", (unsigned)rom->get(addr));
      return 1;
    }
  if (out)
    {
      out->append(name[op]); mne_sep(out);
      if (width == 0)      rname(reg, out);
      else if (width == 1) wrname(reg, out);
      else                 drname(reg, out);
      out->append(",");
      switch (sub & 0x0f)
        {
        case 0x00: out->appendf("#0x%02x", (unsigned)rom->get(addr + 2)); break;
        case 0x01: dir8_out(this, rom->get(addr + 2), out); break;
        case 0x04: out->appendf("#0x%04x",
                                (unsigned)(rom->get(addr+2)*256 + rom->get(addr+3))); break;
        case 0x05: dir8_out(this, rom->get(addr + 2), out); break;
        case 0x08: out->appendf("#0x%04x",
                                (unsigned)(rom->get(addr+2)*256 + rom->get(addr+3))); break;
        case 0x0c: out->appendf("#0xffff%04x",
                                (unsigned)(rom->get(addr+2)*256 + rom->get(addr+3))); break;
        case 0x0d: dir8_out(this, rom->get(addr + 2), out); break;
        case 0x09: out->append("@"); wrname(reg, out); break;
        case 0x0b: out->append("@"); drname(reg, out); break;
        }
    }
  return len;
}


/* --- main decoder: mirrors exec_inst's switch ------------------------- */
int
cl_uc251::disass_251(t_addr addr, chars *out)
{
  t_mem code= rom->get(addr);
  switch (code)
    {
    case 0x00: if (out) out->append("NOP"); return 1;

    case 0x02:        /* LJMP addr16 */
      if (out)
        {
          t_addr target= rom->get(addr+1)*256 + rom->get(addr+2);
          out->append("LJMP"); mne_sep(out);
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;
    case 0x12:        /* LCALL addr16 */
      if (out)
        {
          t_addr target= rom->get(addr+1)*256 + rom->get(addr+2);
          out->append("LCALL"); mne_sep(out);
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;
    case 0x22: if (out) out->append("RET");   return 1;
    case 0x32: if (out) out->append("RETI");  return 1;
    case 0xaa: if (out) out->append("ERET");  return 1;

    /* ADD/ADDC/SUBB/ANL/ORL/XRL A,#data (2 bytes) */
    case 0x24: if (out) { out->append("ADD");  mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;
    case 0x34: if (out) { out->append("ADDC"); mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;
    case 0x94: if (out) { out->append("SUBB"); mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;
    case 0x54: if (out) { out->append("ANL");  mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;
    case 0x44: if (out) { out->append("ORL");  mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;
    case 0x64: if (out) { out->append("XRL");  mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;

    /* ADD/ADDC/SUBB/ANL/ORL/XRL A,dir8 (2 bytes) */
    case 0x25: if (out) { out->append("ADD");  mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x35: if (out) { out->append("ADDC"); mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x95: if (out) { out->append("SUBB"); mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x55: if (out) { out->append("ANL");  mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x45: if (out) { out->append("ORL");  mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x65: if (out) { out->append("XRL");  mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;

    /* ANL/ORL/XRL dir8,A (2 bytes) */
    case 0x52: if (out) { out->append("ANL"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",A"); } return 2;
    case 0x42: if (out) { out->append("ORL"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",A"); } return 2;
    case 0x62: if (out) { out->append("XRL"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",A"); } return 2;
    /* ANL/ORL/XRL dir8,#data (3 bytes) */
    case 0x53: if (out) { out->append("ANL"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",#"); out->appendf("0x%02x",(unsigned)rom->get(addr+2)); } return 3;
    case 0x43: if (out) { out->append("ORL"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",#"); out->appendf("0x%02x",(unsigned)rom->get(addr+2)); } return 3;
    case 0x63: if (out) { out->append("XRL"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",#"); out->appendf("0x%02x",(unsigned)rom->get(addr+2)); } return 3;

    case 0x04: if (out) { out->append("INC"); mne_sep(out); out->append("A"); } return 1;
    case 0x14: if (out) { out->append("DEC"); mne_sep(out); out->append("A"); } return 1;
    case 0x03: if (out) { out->append("RR");  mne_sep(out); out->append("A"); } return 1;
    case 0x13: if (out) { out->append("RRC"); mne_sep(out); out->append("A"); } return 1;
    case 0x23: if (out) { out->append("RL");  mne_sep(out); out->append("A"); } return 1;
    case 0x33: if (out) { out->append("RLC"); mne_sep(out); out->append("A"); } return 1;
    case 0xc4: if (out) { out->append("SWAP"); mne_sep(out); out->append("A"); } return 1;
    case 0xc5: if (out) { out->append("XCH"); mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x05: if (out) { out->append("INC"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0x15: if (out) { out->append("DEC"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0xa3: if (out) { out->append("INC"); mne_sep(out); out->append("DPTR"); } return 1;

    case 0xe4: if (out) { out->append("CLR"); mne_sep(out); out->append("A"); } return 1;
    case 0xf4: if (out) { out->append("CPL"); mne_sep(out); out->append("A"); } return 1;
    case 0xc3: if (out) { out->append("CLR"); mne_sep(out); out->append("C"); } return 1;
    case 0xd3: if (out) { out->append("SETB"); mne_sep(out); out->append("C"); } return 1;
    case 0xb3: if (out) { out->append("CPL"); mne_sep(out); out->append("C"); } return 1;

    case 0xd5:        /* DJNZ dir8,rel */
      if (out)
        {
          t_addr target= (addr + 3 + (i32_t)(i8_t)rom->get(addr+2)) & 0xffffff;
          out->append("DJNZ"); mne_sep(out);
          dir8_out(this, rom->get(addr+1), out); out->append(",");
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;

    case 0xb4:        /* CJNE A,#data,rel */
      if (out)
        {
          t_addr target= (addr + 3 + (i32_t)(i8_t)rom->get(addr+2)) & 0xffffff;
          out->append("CJNE"); mne_sep(out);
          out->append("A,#"); out->appendf("0x%02x,",(unsigned)rom->get(addr+1));
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;
    case 0xb5:        /* CJNE A,dir8,rel */
      if (out)
        {
          t_addr target= (addr + 3 + (i32_t)(i8_t)rom->get(addr+2)) & 0xffffff;
          out->append("CJNE"); mne_sep(out);
          out->append("A,"); dir8_out(this, rom->get(addr+1), out); out->append(",");
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;

    /* bit operations (2-3 bytes) */
    case 0xc2: if (out) { out->append("CLR");  mne_sep(out); baddr_name(rom->get(addr+1), out); } return 2;
    case 0xd2: if (out) { out->append("SETB"); mne_sep(out); baddr_name(rom->get(addr+1), out); } return 2;
    case 0xb2: if (out) { out->append("CPL");  mne_sep(out); baddr_name(rom->get(addr+1), out); } return 2;
    case 0x72: if (out) { out->append("ORL"); mne_sep(out); out->append("C,"); baddr_name(rom->get(addr+1), out); } return 2;
    case 0x82: if (out) { out->append("ANL"); mne_sep(out); out->append("C,"); baddr_name(rom->get(addr+1), out); } return 2;
    case 0xa0: if (out) { out->append("ORL"); mne_sep(out); out->append("C,/"); baddr_name(rom->get(addr+1), out); } return 2;
    case 0xb0: if (out) { out->append("ANL"); mne_sep(out); out->append("C,/"); baddr_name(rom->get(addr+1), out); } return 2;
    case 0xa2: if (out) { out->append("MOV"); mne_sep(out); out->append("C,"); baddr_name(rom->get(addr+1), out); } return 2;
    case 0x92: if (out) { out->append("MOV"); mne_sep(out); baddr_name(rom->get(addr+1), out); out->append(",C"); } return 2;
    case 0x10:        /* JBC bit,rel */
      if (out)
        {
          t_addr target= (addr + 3 + (i32_t)(i8_t)rom->get(addr+2)) & 0xffffff;
          out->append("JBC"); mne_sep(out);
          baddr_name(rom->get(addr+1), out); out->append(",");
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;
    case 0x20:        /* JB bit,rel */
      if (out)
        {
          t_addr target= (addr + 3 + (i32_t)(i8_t)rom->get(addr+2)) & 0xffffff;
          out->append("JB"); mne_sep(out);
          baddr_name(rom->get(addr+1), out); out->append(",");
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;
    case 0x30:        /* JNB bit,rel */
      if (out)
        {
          t_addr target= (addr + 3 + (i32_t)(i8_t)rom->get(addr+2)) & 0xffffff;
          out->append("JNB"); mne_sep(out);
          baddr_name(rom->get(addr+1), out); out->append(",");
          out->appendf(rom->addr_format, target);
          addr_name(target, rom, out);
        }
      return 3;

    case 0x74: if (out) { out->append("MOV"); mne_sep(out); out->append("A,#"); out->appendf("0x%02x",(unsigned)rom->get(addr+1)); } return 2;
    case 0x75: if (out) { out->append("MOV"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",#"); out->appendf("0x%02x",(unsigned)rom->get(addr+2)); } return 3;
    case 0xe5: if (out) { out->append("MOV"); mne_sep(out); out->append("A,"); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0xf5: if (out) { out->append("MOV"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); out->append(",A"); } return 2;
    case 0x85:        /* MOV dir8,dir8 (src first, dst second) */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          dir8_out(this, rom->get(addr+2), out); out->append(",");
          dir8_out(this, rom->get(addr+1), out);
        }
      return 3;
    case 0x90:        /* MOV DPTR,#data16 */
      if (out)
        {
          out->append("MOV"); mne_sep(out);
          out->append("DPTR,#");
          out->appendf("0x%04x",(unsigned)(rom->get(addr+1)*256 + rom->get(addr+2)));
        }
      return 3;

    /* relative branches (2 bytes: opcode + rel8) */
    case 0x60:        /* JZ */
    case 0x70:        /* JNZ */
    case 0x40:        /* JC */
    case 0x50:        /* JNC */
    case 0x80:        /* SJMP */
    case 0x08:        /* JSLE */
    case 0x18:        /* JSG */
    case 0x28:        /* JLE */
    case 0x38:        /* JG */
    case 0x48:        /* JSL */
    case 0x58:        /* JSGE */
    case 0x68:        /* JE */
    case 0x78:        /* JNE */
      {
        static const struct { t_mem op; const char *n; } rel2[]=
          { {0x60,"JZ"},{0x70,"JNZ"},{0x40,"JC"},{0x50,"JNC"},{0x80,"SJMP"},
            {0x08,"JSLE"},{0x18,"JSG"},{0x28,"JLE"},{0x38,"JG"},
            {0x48,"JSL"},{0x58,"JSGE"},{0x68,"JE"},{0x78,"JNE"} };
        const char *n= "SJMP";
        for (unsigned i= 0; i < sizeof(rel2)/sizeof(rel2[0]); i++)
          if (rel2[i].op == code) { n= rel2[i].n; break; }
        if (out)
          {
            t_addr target= (addr + 2 + (i32_t)(i8_t)rom->get(addr+1)) & 0xffffff;
            out->append(n); mne_sep(out);
            out->appendf(rom->addr_format, target);
            addr_name(target, rom, out);
          }
        return 2;
      }

    case 0x0a:        /* MOVZ WRj,Rm (zero-extend) */
    case 0x1a:        /* MOVS WRj,Rm (sign-extend) */
      {
        t_mem b= rom->get(addr+1);
        int dst= b >> 4, src= b & 0x0f;
        if (out)
          {
            out->append(code == 0x0a ? "MOVZ" : "MOVS"); mne_sep(out);
            wrname(dst, out); out->append(",");
            rname(src, out);
          }
        return 2;
      }

    case 0x73: if (out) { out->append("JMP"); mne_sep(out); out->append("@A+DPTR"); } return 1;
    case 0x83: if (out) { out->append("MOVC"); mne_sep(out); out->append("A,@A+PC"); } return 1;
    case 0x93: if (out) { out->append("MOVC"); mne_sep(out); out->append("A,@A+DPTR"); } return 1;

    case 0x89:        /* EJMP @DRk (low nibble must be 8) */
      {
        t_mem sub= rom->get(addr+1);
        if ((sub & 0x0f) == 8)
          {
            if (out)
              {
                out->append("EJMP"); mne_sep(out);
                out->append("@"); drname(sub >> 4, out);
              }
            return 2;
          }
        if (out) out->appendf(".db 0x%02x",(unsigned)code);
        return 1;
      }
    case 0x99:        /* LCALL @WRj (lo4) / ECALL @DRk (lo8) */
      {
        t_mem sub= rom->get(addr+1);
        int reg= sub >> 4;
        if ((sub & 0x0f) == 4)
          {
            if (out) { out->append("LCALL"); mne_sep(out); out->append("@"); wrname(reg, out); }
            return 2;
          }
        if ((sub & 0x0f) == 8)
          {
            if (out) { out->append("ECALL"); mne_sep(out); out->append("@"); drname(reg, out); }
            return 2;
          }
        if (out) out->appendf(".db 0x%02x",(unsigned)code);
        return 1;
      }
    case 0x8a:        /* EJMP addr24 */
      if (out)
        {
          t_addr target= ((t_mem)rom->get(addr+1)<<16) | (rom->get(addr+2)<<8) | rom->get(addr+3);
          out->append("EJMP"); mne_sep(out);
          out->appendf("0x%06x", (unsigned)target);
          addr_name(target, rom, out);
        }
      return 4;
    case 0x9a:        /* ECALL addr24 */
      if (out)
        {
          t_addr target= ((t_mem)rom->get(addr+1)<<16) | (rom->get(addr+2)<<8) | rom->get(addr+3);
          out->append("ECALL"); mne_sep(out);
          out->appendf("0x%06x", (unsigned)target);
          addr_name(target, rom, out);
        }
      return 4;

    case 0xa5: return disass_a5(addr, out);
    case 0x7e: return disass_7e(addr, out);
    case 0x7a: return disass_7a(addr, out);
    case 0x09:
    case 0x29:
    case 0x69:
    case 0x39:
    case 0x59:
    case 0x79: return disass_idx16(addr, code, out);
    case 0x0b: return disass_0b(addr, 0, out);
    case 0x1b: return disass_0b(addr, 1, out);

    case 0xa4: if (out) { out->append("MUL"); mne_sep(out); out->append("AB"); } return 1;
    case 0x84: if (out) { out->append("DIV"); mne_sep(out); out->append("AB"); } return 1;
    case 0xad:        /* MUL WRj,WRms (16x16 -> 32) */
      {
        t_mem sub= rom->get(addr+1);
        if (out)
          {
            out->append("MUL"); mne_sep(out);
            wrname(sub >> 4, out); out->append(","); wrname(sub & 0x0f, out);
          }
        return 2;
      }
    case 0x8d:        /* DIV WRj,WRms (16/16) */
      {
        t_mem sub= rom->get(addr+1);
        if (out)
          {
            out->append("DIV"); mne_sep(out);
            wrname(sub >> 4, out); out->append(","); wrname(sub & 0x0f, out);
          }
        return 2;
      }

    case 0xca: return disass_ca(addr, out);
    case 0xda: return disass_da(addr, out);
    case 0xc0: if (out) { out->append("PUSH"); mne_sep(out); dir8_out(this, rom->get(addr+1), out); } return 2;
    case 0xd0: if (out) { out->append("POP");  mne_sep(out); dir8_out(this, rom->get(addr+1), out); } return 2;

    case 0xe0: if (out) { out->append("MOVX"); mne_sep(out); out->append("A,@DPTR"); } return 1;
    case 0xe2: if (out) { out->append("MOVX"); mne_sep(out); out->append("A,@R0"); } return 1;
    case 0xe3: if (out) { out->append("MOVX"); mne_sep(out); out->append("A,@R1"); } return 1;
    case 0xf0: if (out) { out->append("MOVX"); mne_sep(out); out->append("@DPTR,A"); } return 1;
    case 0xf2: if (out) { out->append("MOVX"); mne_sep(out); out->append("@R0,A"); } return 1;
    case 0xf3: if (out) { out->append("MOVX"); mne_sep(out); out->append("@R1,A"); } return 1;

    /* 2C/9C/5C/4C/6C: ADD/SUB/ANL/ORL/XRL Rmd,Rms (2 bytes) */
    case 0x2c:
    case 0x9c:
    case 0x5c:
    case 0x4c:
    case 0x6c:
      {
        static const struct { t_mem op; const char *n; } t[]=
          { {0x2c,"ADD"},{0x9c,"SUB"},{0x5c,"ANL"},{0x4c,"ORL"},{0x6c,"XRL"} };
        const char *n= "XRL";
        for (unsigned i= 0; i < sizeof(t)/sizeof(t[0]); i++)
          if (t[i].op == code) { n= t[i].n; break; }
        t_mem sub= rom->get(addr+1);
        if (out)
          {
            out->append(n); mne_sep(out);
            rname(sub >> 4, out); out->append(","); rname(sub & 0x0f, out);
          }
        return 2;
      }

    /* 2D/9D/5D/4D/6D (WRj) + 2F/9F/5F/4F/6F (DRk): ADD/SUB/ANL/ORL/XRL (2b) */
    case 0x2d: case 0x9d: case 0x5d: case 0x4d: case 0x6d:
    case 0x2f: case 0x9f: case 0x5f: case 0x4f: case 0x6f:
      {
        static const struct { t_mem op; const char *n; } t[]=
          { {0x2d,"ADD"},{0x9d,"SUB"},{0x5d,"ANL"},{0x4d,"ORL"},{0x6d,"XRL"} };
        int dr= (code & 0x02) != 0;
        const char *n= "XRL";
        for (unsigned i= 0; i < sizeof(t)/sizeof(t[0]); i++)
          if (t[i].op == (code & 0xfd)) { n= t[i].n; break; }
        t_mem sub= rom->get(addr+1);
        int d= sub >> 4, s= sub & 0x0f;
        if (out)
          {
            out->append(n); mne_sep(out);
            if (dr) { drname(d, out); out->append(","); drname(s, out); }
            else    { wrname(d, out); out->append(","); wrname(s, out); }
          }
        return 2;
      }

    /* 2E/4E/5E/6E/9E/BE: ADD/ORL/ANL/XRL/SUB/CMP Rm/WRj/DRk,operand */
    case 0x2e: return disass_alu_rm(addr, 0, out);
    case 0x4e: return disass_alu_rm(addr, 1, out);
    case 0x5e: return disass_alu_rm(addr, 2, out);
    case 0x6e: return disass_alu_rm(addr, 3, out);
    case 0x9e: return disass_alu_rm(addr, 4, out);
    case 0xbe: return disass_alu_rm(addr, 5, out);

    case 0xbc:        /* CMP Rm,Rn (byte compare) */
      {
        t_mem sub= rom->get(addr+1);
        if (out)
          {
            out->append("CMP"); mne_sep(out);
            rname(sub >> 4, out); out->append(","); rname(sub & 0x0f, out);
          }
        return 2;
      }

    case 0x7c: return disass_regmove(addr, 0x7c, out);
    case 0x7d: return disass_regmove(addr, 0x7d, out);
    case 0x7f: return disass_regmove(addr, 0x7f, out);

    default:
      if (out)
        out->appendf(".db 0x%02x", (unsigned)code);
      return 1;
    }
}

/* End of s51.src/uc251.cc */
