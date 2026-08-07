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
    case 3: r= a & b;               acc->write(r); break;
    case 4: r= a | b;               acc->write(r); break;
    case 5: r= a ^ b;               acc->write(r); break;
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
    case 0x08: /* MOV DRk,#0data16 (high 16 bits zero) */
      {
	t_mem h= fetch(), l= fetch();
	set_dr(reg * 4, (h << 8) | l);
	return(resGO);
      }
    case 0x0c: /* MOV DRk,#1data16 (high 16 bits ones) */
      {
	t_mem h= fetch(), l= fetch();
	set_dr(reg * 4, 0xffff0000 | (h << 8) | l);
	return(resGO);
      }
    case 0x0d: /* MOV DRk,dir8 */
      set_dr(reg * 4, read_dir8(fetch()));
      return(resGO);
    default:
      return(inst_unknown(sub));
    }
}


/* 0B/1B-prefixed INC/DEC family: second byte high nibble = register,    */
/* low nibble 0x0 = INC/DEC Rn (direct), 0x1/0x5/0xe = #short forms TODO */

int
cl_uc251::exec_0b(t_mem sub, int dec)
{
  if ((sub & 0x0f) == 0x00)
    {
      t_mem v= get_r8(sub >> 4);
      set_r8(sub >> 4, dec ? v - 1 : v + 1);
      return(resGO);
    }
  return(inst_unknown(sub));
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
    case 0xb3: /* CPL CY */
      bits->set(0xd7, (bits->read(0xd7)) ? 0 : 0x80);
      return(resGO);

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
    case 0x0b: /* INC family */
      return(exec_0b(fetch(), 0));
    case 0x1b: /* DEC family */
      return(exec_0b(fetch(), 1));

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
