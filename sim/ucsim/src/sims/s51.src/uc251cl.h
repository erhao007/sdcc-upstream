/*
 * Simulator of microcontrollers (uc251cl.h)
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

#ifndef UC251CL_HEADER
#define UC251CL_HEADER

#include "ddconfig.h"

#include "uc89c51rcl.h"


/*
 * MCS-251 (Source mode) CPU core.
 *
 * Current implementation status: Phase 3 skeleton.  Instruction decoder and
 * execution cover a verified core subset (NOP, 8-bit arithmetic/logic on A,
 * MOV A/Rn/#data/DPTR, relative branches, LJMP/LCALL, EJMP/ECALL 24-bit,
 * RET/RETI/ERET, and the A5-prefixed A<->Rn moves).  WR/DR register access
 * helpers (big-endian, per Intel 8XC251SB) are provided for the upcoming
 * full instruction set; operand encodings must follow isa/mcs251.yaml.
 *
 * Decoder conventions used so far (from isa/mcs251.yaml, Source mode):
 *   A5 <fn:3><rn:3>: rn in low 3 bits; fn 0x1d=MOV A,Rn, 0x1f=MOV Rn,A,
 *                    fn 0x05=ADD A,Rn
 *   7E <0x50|rn> <imm8>: MOV Rn,#data
 *   9A/8A <addr24>: ECALL/EJMP (big-endian 24-bit)
 */
class cl_uc251: public cl_uc89c51r
{
public:
  cl_uc251(struct cpu_entry *Itype, class cl_sim *asim);

  virtual int exec_inst(void);

  // MCS-251 register file helpers (big-endian WR/DR tuples over IRAM 0x00-0x0F)
  t_mem get_r8(int n);
  void  set_r8(int n, t_mem v);
  t_mem get_wr(int j);                    // j even: (R[j]<<8)|R[j+1]
  void  set_wr(int j, t_mem v);
  t_mem get_dr(int k);                    // k in {0,4,8,12}: 32-bit big-endian
  void  set_dr(int k, t_mem v);

  // PSW flag helpers (8051-compatible CY/AC/OV; N/Z in PSW1, TODO)
  void set_flags_add8(t_mem a, t_mem b, t_mem r);

  // core instruction implementations (Source mode encodings)
  int inst_ret251(void);
  int inst_reti251(void);
  int inst_eret251(void);                 // 24-bit return
  int inst_lcall16(t_mem addr);           // PC already advanced
  int inst_ecall24(t_mem addr);
  int inst_add_a_imm8(void);
  int inst_addc_a_imm8(void);
  int inst_subb_a_imm8(void);
  int inst_anl_a_imm8(void);
  int inst_orl_a_imm8(void);
  int inst_xrl_a_imm8(void);

  int exec_a5(t_mem fnrn);                // A5-prefixed second byte
  int exec_7e(t_mem sub);                 // 7E-prefixed (MOV imm/direct family)

protected:
  class cl_memory_cell *psw1;
};


#endif

/* End of s51.src/uc251cl.h */
