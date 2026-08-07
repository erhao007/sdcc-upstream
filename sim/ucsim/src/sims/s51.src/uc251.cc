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

#include "uc251cl.h"

#include "regs51.h"
#include "types51.h"


/*
 * Making an 251 CPU object
 */

cl_uc251::cl_uc251(struct cpu_entry *Itype, class cl_sim *asim):
  cl_uc89c51r(Itype, asim)
{
  psw1= NULL;
}


/* MCS-251 register file helpers ---------------------------------------- */

t_mem
cl_uc251::get_r8(int n)
{
  return(iram->get_cell(n & 0x0f)->read());
}


void
cl_uc251::set_r8(int n, t_mem v)
{
  iram->get_cell(n & 0x0f)->write(v);
}


/* WRj = {Rj(MSB), Rj+1(LSB)} big-endian, j even */
t_mem
cl_uc251::get_wr(int j)
{
  j &= 0x0e;
  return((get_r8(j) << 8) | get_r8(j + 1));
}


void
cl_uc251::set_wr(int j, t_mem v)
{
  j &= 0x0e;
  set_r8(j, (v >> 8) & 0xff);
  set_r8(j + 1, v & 0xff);
}


/* DRk = {Rk(MSB) ... Rk+3(LSB)} big-endian, k in {0,4,8,12} */
t_mem
cl_uc251::get_dr(int k)
{
  k &= 0x0c;
  return(((t_mem)get_r8(k) << 24) | ((t_mem)get_r8(k + 1) << 16) |
	 ((t_mem)get_r8(k + 2) << 8) | get_r8(k + 3));
}


void
cl_uc251::set_dr(int k, t_mem v)
{
  k &= 0x0c;
  set_r8(k, (v >> 24) & 0xff);
  set_r8(k + 1, (v >> 16) & 0xff);
  set_r8(k + 2, (v >> 8) & 0xff);
  set_r8(k + 3, v & 0xff);
}


/* CY/AC/OV per 8051 semantics (N/Z in PSW1: TODO) */
void
cl_uc251::set_flags_add8(t_mem a, t_mem b, t_mem r)
{
  t_mem newC= ((a + b) > 255) ? 0x80 : 0;
  t_mem newA= ((a & 0x0f) + (b & 0x0f)) & 0xf0;
  t_mem c6  = ((a & 0x7f) + (b & 0x7f)) & 0x80;
  bits->set(0xd7, newC);                        // CY
  SFR_SET_BIT(newC ^ c6, PSW, bmOV);
  SFR_SET_BIT(newA, PSW, bmAC);
}


/* Stack helpers -------------------------------------------------------- */

static void
push_byte(class cl_51core *cpu, class cl_memory_cell *stck, t_mem v, int *sp)
{
  stck->write(v);
}


int
cl_uc251::inst_ret251(void)
{
  t_mem sp= sfr->read(SP);
  t_mem h= iram->get_cell(sp)->read();      // stack top = high byte
  sp= sfr->write(SP, sp - 1);
  t_mem l= iram->get_cell(sp)->read();
  sp= sfr->write(SP, sp - 1);
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
  t_mem sp= sfr->read(SP);
  t_mem h= iram->get_cell(sp)->read();
  sp= sfr->write(SP, sp - 1);
  t_mem m= iram->get_cell(sp)->read();
  sp= sfr->write(SP, sp - 1);
  t_mem l= iram->get_cell(sp)->read();
  sp= sfr->write(SP, sp - 1);
  PC= (h << 16) | (m << 8) | l;
  vc.rd+= 3;
  return(resGO);
}


int
cl_uc251::inst_lcall16(t_mem addr)
{
  t_mem sp_before= sfr->get(SP);
  t_mem sp= sfr->write(SP, sfr->read(SP) + 1);
  iram->get_cell(sp)->write(PC & 0xff);         // push low byte
  sp= sfr->write(SP, sfr->read(SP) + 1);
  iram->get_cell(sp)->write((PC >> 8) & 0xff);  // push high byte
  PC= addr;
  class cl_stack_op *so= new cl_stack_call(instPC, PC, PC, sp_before, sp);
  so->init();
  stack_write(so);
  vc.wr+= 2;
  return(resGO);
}


int
cl_uc251::inst_ecall24(t_mem addr)
{
  t_mem sp_before= sfr->get(SP);
  t_mem sp= sfr->write(SP, sfr->read(SP) + 1);
  iram->get_cell(sp)->write(PC & 0xff);
  sp= sfr->write(SP, sfr->read(SP) + 1);
  iram->get_cell(sp)->write((PC >> 8) & 0xff);
  sp= sfr->write(SP, sfr->read(SP) + 1);
  iram->get_cell(sp)->write((PC >> 16) & 0xff);
  PC= addr;
  class cl_stack_op *so= new cl_stack_call(instPC, PC, PC, sp_before, sp);
  so->init();
  stack_write(so);
  vc.wr+= 3;
  return(resGO);
}


/* 8-bit arithmetic/logic on A with 8-bit immediate ---------------------- */

int
cl_uc251::inst_add_a_imm8(void)
{
  t_mem data= fetch();
  t_mem ac= acc->read();
  t_mem r= ac + data;
  acc->write(r);
  set_flags_add8(ac, data, r);
  return(resGO);
}


int
cl_uc251::inst_addc_a_imm8(void)
{
  t_mem data= fetch();
  t_mem c= (bits->read(0xd7)) ? 1 : 0;
  t_mem ac= acc->read();
  t_mem r= ac + data + c;
  acc->write(r);
  set_flags_add8(ac, data + c, r);
  return(resGO);
}


int
cl_uc251::inst_subb_a_imm8(void)
{
  t_mem data= fetch();
  t_mem c= (bits->read(0xd7)) ? 1 : 0;
  t_mem ac= acc->read();
  t_mem r= ac - data - c;
  acc->write(r);
  /* borrow -> CY; OV/AC simplified (TODO verify) */
  bits->set(0xd7, (ac < (data + c)) ? 0x80 : 0);
  SFR_SET_BIT(((ac ^ data ^ r) & 0x80) ? 1 : 0, PSW, bmOV);
  SFR_SET_BIT(((ac & 0x0f) < ((data + c) & 0x0f)) ? 1 : 0, PSW, bmAC);
  return(resGO);
}


int
cl_uc251::inst_anl_a_imm8(void)
{
  t_mem data= fetch();
  acc->write(acc->read() & data);
  return(resGO);
}


int
cl_uc251::inst_orl_a_imm8(void)
{
  t_mem data= fetch();
  acc->write(acc->read() | data);
  return(resGO);
}


int
cl_uc251::inst_xrl_a_imm8(void)
{
  t_mem data= fetch();
  acc->write(acc->read() ^ data);
  return(resGO);
}


/* A5-prefixed instructions (Source mode) -------------------------------- */

int
cl_uc251::exec_a5(t_mem fnrn)
{
  int rn= fnrn & 0x07;
  switch (fnrn >> 3)
    {
    case 0x05: /* ADD A,Rn */
      {
	t_mem ac= acc->read();
	t_mem r= ac + get_r8(rn);
	acc->write(r);
	set_flags_add8(ac, get_r8(rn), r);
	return(resGO);
      }
    case 0x1d: /* MOV A,Rn */
      acc->write(get_r8(rn));
      return(resGO);
    case 0x1f: /* MOV Rn,A */
      set_r8(rn, acc->read());
      return(resGO);
    default:
      return(inst_unknown(fnrn));
    }
}


/* 7E-prefixed: MOV Rm/Rn,#data (sub = (reg<<4)|0x00), etc. --------------- */

int
cl_uc251::exec_7e(t_mem sub)
{
  if ((sub & 0x0f) == 0x00) /* MOV Rm,#data (Rn 0-7 is a subset of Rm) */
    {
      t_mem data= fetch();
      set_r8(sub >> 4, data);
      return(resGO);
    }
  return(inst_unknown(sub));
}


/* Main decode/execute --------------------------------------------------- */

int
cl_uc251::exec_inst(void)
{
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

    case 0x24: return(inst_add_a_imm8());
    case 0x34: return(inst_addc_a_imm8());
    case 0x94: return(inst_subb_a_imm8());
    case 0x54: return(inst_anl_a_imm8());
    case 0x44: return(inst_orl_a_imm8());
    case 0x64: return(inst_xrl_a_imm8());

    case 0x74: /* MOV A,#data */
      acc->write(fetch());
      return(resGO);

    case 0x75: /* MOV dir8,#data (dir8 0x00-0x7f -> IRAM, 0x80+ -> SFR) */
      {
	t_mem addr= fetch();
	t_mem data= fetch();
	if (addr < 0x80)
	  iram->write(addr, data);
	else
	  sfr->write(addr, data);
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
    case 0x80: /* SJMP rel */
      {
	t_mem rel= fetch();
	PC= rom->validate_address(PC + (signed char)rel);
	return(resGO);
      }
    case 0x73: /* JMP @A+DPTR */
      PC= (acc->read() + (sfr->read(DPH) << 8) + sfr->read(DPL));
      return(resGO);

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

    default:
      return(inst_unknown(code));
    }
}

/* End of s51.src/uc251.cc */
