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
 * Current implementation status: Phase 3 iteration 2.  Instruction decoder
 * and execution cover the 8-bit core: MOV (A/Rn/Rm/dir8/@Ri/WRj/DRk, all
 * immediate/direct/register forms in the supported subset), 8-bit
 * ADD/ADDC/SUBB/ANL/ORL/XRL (A with #data/dir8/Rn/@Ri, plus dir8 A/imm8
 * forms), INC/DEC (A/Rn/dir8/@Ri), CLR/CPL (A, CY), relative branches,
 * LJMP/LCALL, EJMP/ECALL 24-bit, RET/RETI/ERET, JMP @A+DPTR, and register
 * moves Rm,Rm / WRj,WRj / DRk,DRk.  WR/DR register helpers are big-endian
 * (Intel 8XC251SB).  Operand encodings follow isa/mcs251.yaml.
 *
 * Decoder conventions (from isa/mcs251.yaml, Source mode):
 *   A5 <fn:5><rn:3>/<ri:1>: low bits select Rn or Ri(0/1); fn table covers
 *     MOV/ALU between A and Rn/@Ri, MOV @Ri,dir8/imm8, INC/DEC @Ri
 *   7E <reg:4><type:4>: Rm direct (0-15), WRj = reg*2, DRk = reg*4;
 *     type 0x0=#data8, 0x1=dir8, 0x4=#data16, 0x5=dir8, 0x8=#0data16,
 *     0xc=#1data16, 0xd=dir8(DRk)
 *   7C/7D/7F <d:4><s:4>: Rm/Rn, WRj, DRk register moves
 *   0B/1B <reg:4><0x0>: INC/DEC Rn
 *   9A/8A <addr24>: ECALL/EJMP (big-endian 24-bit)
 */
class cl_uc251: public cl_uc89c51r
{
public:
  cl_uc251(struct cpu_entry *Itype, class cl_sim *asim);

  virtual int exec_inst(void);
  virtual void make_address_spaces(void);

  // MCS-251 register file helpers.
  // Register file layout (Intel 8XC251SB User's Manual, Ch.3):
  //   R0-R7   bank-selected, mapped to IRAM 0x00-0x1F (4 banks x 8 bytes)
  //   R8-R31  CPU register file (not mapped to IRAM); R11 is an alias of ACC
  //   R56-R63 DPX/SPX etc. (handled in get_dr/set_dr aliases)
  // SDCC/mcs251 uses bank 0 only, so R0-R7 = IRAM[0..7].
  t_mem get_r8(int n);
  void  set_r8(int n, t_mem v);
  t_mem get_wr(int j);                    // j even: (R[j]<<8)|R[j+1]
  void  set_wr(int j, t_mem v);
  t_mem get_dr(int k);                    // k multiple of 4: 32-bit big-endian
  void  set_dr(int k, t_mem v);

  // memory access helpers
  t_mem read_dir8(t_mem addr);            // dir8: 0x00-0x7f IRAM, 0x80+ SFR
  void  write_dir8(t_mem addr, t_mem v);
  t_mem read_ri(int ri);                  // @Ri indirect (Ri in {R0,R1})
  void  write_ri(int ri, t_mem v);
  t_mem read_edata(t_addr addr);          // EDATA (stack/SPX space) via xram
  void  write_edata(t_addr addr, t_mem v);
  t_mem read_spx_dis16(t_addr dis);       // edata[SPX+dis16]
  void  write_spx_dis16(t_addr dis, t_mem v);

  // SPX (16-bit stack pointer extension); edata base for @SPX addressing
  t_mem spx;

  // PSW flag helpers (8051-compatible CY/AC/OV; N/Z in PSW1, TODO)
  void set_flags_add8(t_mem a, t_mem b, t_mem r);
  void set_nz(t_mem r, int width);         // set PSW1 N/Z for width-byte result
  int  get_n(void);                        // PSW1.7
  int  get_z(void);                        // PSW1.6

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
  int inst_alu_a_imm8(int op);            // shared: add/addc/subb/anl/orl/xrl A,#data
  int inst_alu_a_op8(int op, t_mem b);    // shared: A op= b (op 0=add,1=addc,2=subb,
                                          //         3=anl,4=orl,5=xrl)

  int exec_a5(t_mem fnrn);                // A5-prefixed second byte
  int exec_7e(t_mem sub);                 // 7E-prefixed (MOV imm/direct family)
  int exec_0b(t_mem sub, int dec);        // 0B/1B-prefixed (INC/DEC family)

protected:
  t_mem psw1;                       // MCS-251 PSW1: bit7=N, bit6=Z (SFR 0xA0)
  t_mem rfile[24];                  // CPU register file R8-R31 (R11 alias of ACC)
};


#endif

/* End of s51.src/uc251cl.h */
