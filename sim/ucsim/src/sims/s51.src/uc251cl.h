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
  virtual void make_chips(void);            // chains to inherited, then adds eaxfr_chip
  virtual void make_memories(void);         // chains to inherited, then decode_eaxfr()

  // Disassembly: override the inherited 8051 table decoder with a real
  // MCS-251 Source-mode decoder.  Without this, uCsim decodes MCS-251 bytes
  // using the 8051 table, so any prefix byte (A5/7E/7A/7C-7F/0B-1B/09/29/
  // .../89/99/BC/CA/DA) is split into separate 8051 ops and the instruction
  // boundary is lost (e.g. 7E 74 71 82 shows as 4 unrelated 8051 ops instead
  // of one "MOV WR14,#0x7182").  inst_length/longest_inst are overridden so
  // the dc loop and print_disass advance by the true MCS-251 step.
  virtual char *disass(t_addr addr);
  virtual int inst_length(t_addr addr);
  virtual int longest_inst(void);

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
  t_mem read_edata_ram(t_addr addr);      // EDATA RAM (no von-Neumann ROM mirror)
  void  write_edata(t_addr addr, t_mem v);
  t_mem read_spx_dis16(t_addr dis);       // edata[SPX+dis16]
  void  write_spx_dis16(t_addr dis, t_mem v);

  // Override set_rom to record which ROM cells were loaded from the hex
  // file.  read_edata's von-Neumann mirror needs this to distinguish a
  // genuine 0xFF data byte (loaded code/const) from an unprogrammed 0xFF
  // (empty ROM), so that e.g. crtxinit copying a float constant whose
  // bytes include 0xFF does not fetch garbage from xram instead.
  virtual bool set_rom(class cl_inspec *is, t_addr addr, t_mem val, bool check);
  bool rom_loaded_p(t_addr addr);         // true if ROM[addr] was loaded

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
  // Override the 3-arg virtual (cl_51core::inst_lcall) used by accept_it to
  // enter an ISR.  The base pushes a 16-bit PC to iram[SP]; MCS-251 ISRs
  // return via ERET (3-byte pop from spx), so push the full 24-bit PC onto
  // the SPX/edata stack instead.
  int inst_lcall(t_mem code, uint addr, bool intr);
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
  // Bitmap of ROM cells loaded from the hex file (1 bit per ROM byte).
  // Used by read_edata's von-Neumann mirror to tell loaded 0xFF data from
  // empty (erased) ROM.  Covers the full 128 KiB ROM window.
  unsigned char rom_loaded[0x20000 / 8];

  // EAXFR (extended SFR) region 0x7E0000-0x7EFFFF: extended peripherals
  // (I2C/PWM/DMA/CAN) declared in stc32g12k128.h as __xdata __at(0x7E....).
  // Firmware reaches them via a 24-bit @dpx pointer (DPXL=0x7E), which
  // write_edata/read_edata route.  Without this dedicated backing store
  // those addresses silently alias xram[addr & 0xffff].  Wired as a proper
  // three-piece (address space + chip + decoder) so the cells are decoded
  // (CELL_NON_DECODED cleared) and the region shows up in `info mem` /
  // VCD.  Note: P_SW2.EAXFR (bit 7 of SFR 0xBA) gates access on real
  // hardware; the simulator does NOT enforce the gate (it routes
  // 0x7E0000+ unconditionally; disabled-state behaviour is unverified).
  class cl_address_space *eaxfr;
  class cl_memory_chip *eaxfr_chip;
  void decode_eaxfr(void);

  // XDATA region 0x010000-0x01FFFF: the real external SRAM (SDCC default
  // xdata_loc).  Firmware reaches it via a 24-bit @dpx pointer (DPXL=0x01),
  // routed by write_edata/read_edata.  Previously region 01 folded into the
  // same xram[addr & 0xffff] backing as the edata window (region 00), so e.g.
  // xdata 0x01F000 aliased edata 0x00F000.  This dedicated three-piece store
  // de-aliases them.  crtxinit/crtxclear (which use @dpx, DPXL=0x01) target
  // this store; the edata window and von-Neumann ROM mirror (region 00) are
  // unchanged.  read_edata_ram is left routing to xram (its callers are all
  // 16-bit SPX stack addresses that cannot reach region 01).  MOVX (legacy
  // 8051-compat, unused by mcs251 codegen) still hits xram directly.
  class cl_address_space *xdata;
  class cl_memory_chip *xdata_chip;
  void decode_xdata(void);

  // --- Disassembly decode (private; mirrors exec_inst's dispatch tree) ---
  // disass_251 decodes the instruction whose opcode is at PC=addr.  It writes
  // the mnemonic+operands into *out (if non-NULL) using the same column
  // convention as cl_51core::disass (mnemonic padded to 6 cols, then one
  // separating space, then operands), and returns the instruction length in
  // bytes.  Unknown/illegal bytes return length 1 and emit ".db 0xNN" so the
  // dc loop always advances.  disass() and inst_length() both delegate here.
  int disass_251(t_addr addr, chars *out);
  // Per-family sub-decoders (each reads operand bytes via rom->get at fixed
  // offsets from addr; returns the total length including the prefix byte).
  int disass_a5(t_addr addr, chars *out);              // A5 prefix
  int disass_7e(t_addr addr, chars *out);              // 7E prefix (MOV family)
  int disass_7a(t_addr addr, chars *out);              // 7A prefix (store forms)
  int disass_regmove(t_addr addr, int code, chars *out); // 7C/7D/7F
  int disass_0b(t_addr addr, int dec, chars *out);     // 0B/1B (INC/DEC + WRj move)
  int disass_idx16(t_addr addr, int code, chars *out); // 09/29/39/59/69/79
  int disass_ca(t_addr addr, chars *out);              // CA (PUSH family)
  int disass_da(t_addr addr, chars *out);              // DA (POP family)
  int disass_alu_rm(t_addr addr, int op, chars *out);  // 2E/4E/5E/6E/9E/BE
  // Register-name helpers (write the name into *out).
  static void rname(int idx, chars *out);              // Rn
  static void wrname(int reg, chars *out);             // WRj, j=reg*2
  static void drname(int reg, chars *out);             // DRk, k=reg*4
  static void riname(int ri, chars *out);              // @Ri (ri&1)
};


#endif

/* End of s51.src/uc251cl.h */
