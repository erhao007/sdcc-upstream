/*------------------------------------------------------------------------

  ralloc2.cc - register allocator for the MCS-251 port (MT-1C)

  Reuses SDCC's shared register-allocation framework
  (SDCCralloc.hpp / SDCCsalloc.hpp): the control-flow graph, the
  byte-level conflict graph and the liveness data all come from
  create_cfg().  Assignment is a port-side greedy walk over that shared
  graph; no second allocator framework or tree-decomposition path is
  introduced.

  Scope of this step (roadmap MT-1C):
    - model the full legacy register pool R0..R9 + R12..R15 (R10/R11
      stay unallocatable, R16..R31 stay closed);
    - model WR/DR overlap as consecutive-byte tuples with legal starts
      and big-endian byte order, never as a second physical file;
    - assign registers over the byte-level conflict graph (greedy,
      legacy preference order, native-MUL result exclusion) and spill
      everything that does not fit (legacy-compatible sloc slots);
    - compute per-iCode rMask/rSurv and currFunc->regsUsed so the
      unmodified gen.c caller-save/callee-save/ISR machinery works;
    - fixed raw temporaries (ACC/B/DPL/DPH/DPXL, DR20/DR24/DR28,
      CY/b0..b7) stay outside the pool, modelled as clobber classes.

  The default compilation path keeps using ralloc.c.  The directed
  adapter at the bottom is linked only into the gate's temporary
  compiler (see tests/check-ralloc2-directed.py).

  The shared framework's compile-time bound MAX_NUM_REGS == 9 in
  SDCCralloc.hpp is too small for the 14-slot pool.  This file does
  not raise that shared bound; the assignment works directly on the
  conflict graph and never materialises an i_assignment_t.  Raising
  the bound remains a proposal for a later step.

  The freestanding descriptor/constraint core below carries no SDCC
  dependencies and is unit-tested standalone via
  -DMCS251_RALLOC2_STANDALONE_TEST.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#ifndef MCS251_RALLOC2_STANDALONE_TEST

#include "SDCCralloc.hpp"
#include "SDCCsalloc.hpp"

extern "C"
{
#include "common.h"
#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"
}

#endif /* MCS251_RALLOC2_STANDALONE_TEST */

namespace mcs251_ralloc2 {

/* Compile-time bound of the shared framework (SDCCralloc.hpp).  Duplicated
   as a plain constant so the standalone unit test can reason about it
   without SDCC headers. */
static const int kMaxFrameworkRegs = 9;

/* Full legacy byte pool: R0..R9 + R12..R15 (MT-1A facts).  R10/R11 are
   permanently unallocatable (B/ACC aliases); R16..R31 are closed. */
static const int kPoolByteRegs = 14;

struct byte_reg_desc
{
  const char *name;
  int rnum;            /* architectural register number 0..15 */
  bool ptr_capable;    /* legacy REG_PTR designation (R0/R1) */
};

/* Legacy allocation-table order (ralloc.c mcs251_regs[]): R7..R0,
   R15..R12, R9, R8. */
static const byte_reg_desc pool_byte_regs[kPoolByteRegs] =
{
  { "r7",  7,  false },
  { "r6",  6,  false },
  { "r5",  5,  false },
  { "r4",  4,  false },
  { "r3",  3,  false },
  { "r2",  2,  false },
  { "r1",  1,  true  },
  { "r0",  0,  true  },
  { "r15", 15, false },
  { "r14", 14, false },
  { "r13", 13, false },
  { "r12", 12, false },
  { "r9",  9,  false },
  { "r8",  8,  false }
};

/* WR/DR overlap model (MT-1A facts): a word is an even-start pair of
   consecutive byte registers, MSB at the lower number; a dword is a
   4-aligned run of four.  Preference order mirrors the legacy
   candidate lists.  No second physical register file exists. */
static const int word_starts[7] = { 6, 4, 2, 14, 12, 8, 0 };
static const int dword_starts[3] = { 4, 12, 0 };

static inline bool
byte_rnum_in_pool (int rnum)
{
  return (rnum >= 0 && rnum <= 9) || (rnum >= 12 && rnum <= 15);
}

static inline bool
word_start_legal (int start)
{
  if (start % 2 || start < 0 || start > 14 || start == 10)
    return false;
  for (int i = 0; i < 7; ++i)
    if (word_starts[i] == start)
      return true;
  return false;
}

static inline bool
dword_start_legal (int start)
{
  if (start % 4 || start < 0 || start > 12)
    return false;
  for (int i = 0; i < 3; ++i)
    if (dword_starts[i] == start)
      return true;
  return false;
}

/* Fixed raw temporaries stay outside the pool: ACC/B (= R11/R10),
   DPL/DPH/DPXL, DR20/DR24/DR28, DR16 (startup XINIT only), DR60
   (SPX alias).  Modelled as clobber classes for the unit tests. */
enum clobber_class
{
  clobber_none,
  clobber_call,        /* destroyed by any call (caller-save) */
  clobber_native_mul,  /* destroyed by native mul wr12,wr8 (R8..R15) */
  clobber_never        /* reserved, never allocated */
};

static inline clobber_class
fixed_temporal_clobber (int rnum)
{
  switch (rnum)
    {
    case 10: case 11:                 /* B, ACC */
    case 20: case 24: case 28:        /* DR20/DR24/DR28 dwords */
      return clobber_call;
    default:
      return clobber_none;
    }
}

static inline clobber_class
byte_clobber (int rnum)
{
  if (rnum >= 8 && rnum <= 15)
    return clobber_native_mul;        /* native mul scratch/output */
  return fixed_temporal_clobber (rnum);
}

/* Operand admission: plain/float scalars of 1..4 bytes may live in the
   pool; pointers, bits, aggregates and wider scalars spill. */
enum operand_kind
{
  kind_plain,
  kind_pointer,
  kind_bit,
  kind_aggregate,
  kind_float
};

enum admit_decision
{
  admit_in_reg,
  admit_spill,
  admit_fail
};

static inline admit_decision
admit_operand (int size, operand_kind kind)
{
  if (size <= 0 || size > 8)
    return admit_fail;

  switch (kind)
    {
    case kind_plain:
    case kind_float:
      if (size <= 4)
        return admit_in_reg;
      return admit_spill;
    case kind_pointer:
    case kind_aggregate:
    case kind_bit:
      return admit_spill;
    default:
      return admit_fail;
    }
}

/* Tuple preference cost (lower preferred), legacy order. */
static inline float
tuple_cost (int start)
{
  switch (start)
    {
    case 6:  return 0.0f;
    case 4:  return 1.0f;
    case 2:  return 2.0f;
    case 14: return 3.0f;
    case 12: return 4.0f;
    case 8:  return 5.0f;
    default: return 6.0f;
    }
}

/* MT-1C enables assignment selection. */
static inline bool
assignment_selection_enabled (void)
{
  return true;
}

} /* namespace mcs251_ralloc2 */

#ifndef MCS251_RALLOC2_STANDALONE_TEST

static_assert (mcs251_ralloc2::kPoolByteRegs >
               mcs251_ralloc2::kMaxFrameworkRegs,
               "MT-1C works around the shared MAX_NUM_REGS bound via a "
               "conflict-graph-side assignment; if this ever becomes "
               "false the workaround is obsolete");

/*------------------------------------------------------------------------
 * Framework hooks required by create_cfg() (port-provided).
 *------------------------------------------------------------------------*/

/* The shared framework calls this while building each CFG node; the
   port adds result-vs-operand conflict edges for iCodes whose native
   forms are destructive.  The list mirrors the f8 port's conservative
   set; the MCS-251 generator itself re-verifies every native tuple
   before use, so extra edges only cost registers, never correctness. */
template <class I_t>
static void
add_operand_conflicts_in_node (const cfg_node &n, I_t &I)
{
  const iCode *ic = n.ic;

  const operand *result = IC_RESULT (ic);
  const operand *left = IC_LEFT (ic);
  const operand *right = IC_RIGHT (ic);

  if (!result || !IS_SYMOP (result))
    return;

  if (!(ic->op == '+' || ic->op == '-' ||
        (ic->op == UNARYMINUS && !IS_FLOAT (operandType (left))) ||
        ic->op == '^' || ic->op == '|' || ic->op == BITWISEAND ||
        ic->op == GET_VALUE_AT_ADDRESS))
    return;

  operand_map_t::const_iterator oir, oir_end, oirs;
  boost::tie (oir, oir_end) =
    n.operands.equal_range (OP_SYMBOL_CONST (result)->key);
  if (oir == oir_end)
    return;

  operand_map_t::const_iterator oio, oio_end;

  if (left && IS_SYMOP (left))
    for (boost::tie (oio, oio_end) =
           n.operands.equal_range (OP_SYMBOL_CONST (left)->key);
         oio != oio_end; ++oio)
      for (oirs = oir; oirs != oir_end; ++oirs)
        {
          var_t rvar = oirs->second;
          var_t ovar = oio->second;
          if (I[rvar].byte < I[ovar].byte)
            boost::add_edge (rvar, ovar, I);
        }

  if (right && IS_SYMOP (right))
    for (boost::tie (oio, oio_end) =
           n.operands.equal_range (OP_SYMBOL_CONST (right)->key);
         oio != oio_end; ++oio)
      for (oirs = oir; oirs != oir_end; ++oirs)
        {
          var_t rvar = oirs->second;
          var_t ovar = oio->second;
          if (I[rvar].byte < I[ovar].byte)
            boost::add_edge (rvar, ovar, I);
        }
}

/* The shared framework calls this to let the port mark iCodes whose
   code is generated as a side effect of another iCode.  The MCS-251
   generator has no such coupling that the conflict-graph assignment
   needs to know about, so this stays a no-op. */
static void
extra_ic_generated (iCode *)
{
}

/* Per-function spill-slot counter (legacy _G.slocNum counterpart). */
static int mcs251_ralloc2_sloc_num = 0;

/*------------------------------------------------------------------------
 * Spilling: replicate the legacy createStackSpil storage policy so the
 * unmodified gen.c memory paths work.  allocLocal() honours
 * options.stackAuto, so reentrant and stack-auto functions get
 * reentrant-safe stack slots.
 *------------------------------------------------------------------------*/
static void
spill_this (symbol *sym)
{
  symbol *sloc;
  struct dbuf_s dbuf;
  int useXstack, model;

  if (sym->remat)
    return;

  if (sym->usl.spillLoc)
    {
      /* A spill location already exists (left by earlier pipeline
         stages).  Adopt it instead of leaving the symbol in a
         half-spilt state that would break the assigned-or-spilt
         invariant expected by gen.c. */
      sym->isspilt = sym->spillA = 1;
      if (!sym->remat)
        sym->usl.spillLoc->allocreq++;
      return;
    }

  dbuf_init (&dbuf, 128);
  dbuf_printf (&dbuf, "sloc%d", mcs251_ralloc2_sloc_num++);
  sloc = newiTemp (dbuf_c_str (&dbuf));
  dbuf_destroy (&dbuf);

  sloc->type = copyLinkChain (sym->type);
  sloc->etype = getSpec (sloc->type);
  if (!IS_BIT (sloc->etype))
    SPEC_SCLS (sloc->etype) =
      port->mem.default_local_map == xdata ? S_XDATA : S_DATA;
  else if (SPEC_SCLS (sloc->etype) == S_SBIT)
    SPEC_SCLS (sloc->etype) = S_BIT;
  SPEC_EXTR (sloc->etype) = 0;
  SPEC_STAT (sloc->etype) = 0;
  SPEC_VOLATILE (sloc->etype) = 0;
  SPEC_ABSA (sloc->etype) = 0;

  useXstack = options.useXstack;
  model = options.model;
  options.model = options.useXstack = 0;
  allocLocal (sloc);
  options.useXstack = useXstack;
  options.model = model;

  sloc->isref = 1;
  if (IN_STACK (sloc->etype))
    currFunc->stack += getSize (sloc->type);

  sym->usl.spillLoc = sloc;
  sym->isspilt = sym->spillA = sym->stackSpil = 1;
  if (!sym->remat)
    sloc->allocreq++;
  sloc->isFree = 0;
  addSetHead (&sloc->usl.itmpStack, sym);
}

/*------------------------------------------------------------------------
 * Native-mul result exclusion (replica of the ralloc.c logic): results
 * of unsigned 16x16->32 multiplies must avoid R8..R15 because the
 * native sequence uses them as fixed scratch.
 *------------------------------------------------------------------------*/
static bool
is_native_mul_result (const symbol *sym)
{
  int key;

  if (!sym || sym->nRegs != 4 || !sym->defs)
    return false;

  for (key = 0; key < sym->defs->size; ++key)
    {
      iCode *ic;

      if (!bitVectBitValue (sym->defs, key) ||
          !(ic = (iCode *) (hTabItemWithKey (iCodehTab, key))) ||
          ic->op != '*' ||
          !IC_LEFT (ic) || !IC_RIGHT (ic) || !IC_RESULT (ic) ||
          !IS_SYMOP (IC_RESULT (ic)) ||
          OP_SYMBOL (IC_RESULT (ic)) != sym ||
          getSize (operandType (IC_LEFT (ic))) != 2 ||
          getSize (operandType (IC_RIGHT (ic))) != 2 ||
          getSize (operandType (IC_RESULT (ic))) != 4 ||
          !SPEC_USIGN (getSpec (operandType (IC_LEFT (ic)))) ||
          !SPEC_USIGN (getSpec (operandType (IC_RIGHT (ic)))))
        continue;

      return true;
    }

  return false;
}

/*------------------------------------------------------------------------
 * Assignment over the byte-level conflict graph.  Vertices are
 * (live range, byte) pairs created by the shared framework; an edge
 * means the two bytes may not share a register.  Greedy in live-range
 * order, legacy tuple preference, contiguous-tuple-first with a
 * bytes-anywhere fallback (gen.c is placement-agnostic for anything
 * the pool admits), spill otherwise.  Returns the per-vertex register
 * table index (-1 when the live range was spilt).
 *------------------------------------------------------------------------*/
template <class I_t>
static std::vector<int>
ralloc2_assign (const I_t &I)
{
  const unsigned int n = boost::num_vertices (I);
  std::vector<int> vreg (n, -1);
  std::map<int, std::vector<int> > lr_vertices;
  std::vector<symbol *> order_syms;
  std::vector<std::vector<int> *> order_vertices;

  for (unsigned int v = 0; v < n; ++v)
    lr_vertices[I[v].v].push_back (v);

  for (auto &kv : lr_vertices)
    {
      symbol *sym = (symbol *) hTabItemWithKey (liveRanges, kv.first);
      if (!sym)
        continue;
      /* Sort bytes ascending so regs[] is filled LSB-first. */
      std::sort (kv.second.begin (), kv.second.end (),
                 [&] (unsigned int a, unsigned int b)
                 { return I[a].byte < I[b].byte; });
      order_syms.push_back (sym);
      order_vertices.push_back (&kv.second);
    }

  /* Live ranges in definition order, mirroring the legacy serial
     flavour (stable for equal keys). */
  {
    std::vector<unsigned> idx (order_syms.size ());
    for (unsigned i = 0; i < idx.size (); ++i)
      idx[i] = i;
    std::stable_sort (idx.begin (), idx.end (),
                      [&] (unsigned a, unsigned b)
                      { return order_syms[a]->liveFrom <
                              order_syms[b]->liveFrom; });
    std::vector<symbol *> s2;
    std::vector<std::vector<int> *> v2;
    for (unsigned i : idx)
      {
        s2.push_back (order_syms[i]);
        v2.push_back (order_vertices[i]);
      }
    order_syms.swap (s2);
    order_vertices.swap (v2);
  }

  for (unsigned li = 0; li < order_syms.size (); ++li)
    {
      symbol *sym = order_syms[li];
      std::vector<int> &verts = *order_vertices[li];
      const int size = sym->nRegs;
      bool placed = false;
      const bool avoid_r8_r15 =
#ifdef MCS251_RALLOC2_DISABLE_NATIVE_MUL_EXCLUSION
        false;
#else
        is_native_mul_result (sym);
#endif

      /* Invariant: the framework created one vertex per byte.  Any
         mismatch means the live range must fail safe to a spill. */
      if (verts.size () != (unsigned int) size)
        {
          for (int b = 0; b < size; ++b)
            sym->regs[b] = 0;
          spill_this (sym);
          continue;
        }

      auto byte_free = [&] (int vertex, int rnum)
      {
        if (!mcs251_ralloc2::byte_rnum_in_pool (rnum) ||
            (mcs251_ptrRegReq && rnum <= 1) ||
            (avoid_r8_r15 && rnum >= 8))
          return false;
        typename boost::graph_traits<I_t>::adjacency_iterator ai, ae;
        for (boost::tie (ai, ae) = boost::adjacent_vertices (vertex, I);
             ai != ae; ++ai)
          {
            const int other = vreg[*ai];
            if (other >= 0 && mcs251_regs[other].offset == rnum)
              return false;
          }
        return true;
      };

      auto commit = [&] (const std::vector<int> &rn)
      {
        for (int b = 0; b < size; ++b)
          {
            const int ridx = mcs251_regIdxForNumber (rn[b]);
            vreg[verts[b]] = ridx;
            sym->regs[b] = mcs251_regWithIdx (ridx);
            if (currFunc)
              currFunc->regsUsed =
                bitVectSetBit (currFunc->regsUsed, ridx);
          }
      };

      /* Contiguous legal tuples first (word/dword). */
      if (size == 2 || size == 4)
        {
          const int nstarts = size == 2 ? 7 : 3;
          const int *starts =
            size == 2 ? mcs251_ralloc2::word_starts
                      : mcs251_ralloc2::dword_starts;
          for (int s = 0; s < nstarts && !placed; ++s)
            {
              const int start = starts[s];
              bool ok = true;
              std::vector<int> rn (size);
              for (int b = 0; b < size && ok; ++b)
                {
                  /* byte 0 is the LSB and lives at the highest number */
                  rn[b] = start + (size - 1 - b);
                  if (!byte_free (verts[b], rn[b]))
                    ok = false;
                }
              if (ok)
                {
                  commit (rn);
                  placed = true;
                }
            }
        }

      /* Bytes-anywhere fallback in legacy table order. */
      if (!placed)
        {
          bool ok = true;
          std::vector<int> rn (size, -1);
          for (int b = 0; b < size && ok; ++b)
            {
              bool found = false;
              for (int c = 0; c < mcs251_ralloc2::kPoolByteRegs && !found;
                   ++c)
                {
                  const int rnum = mcs251_ralloc2::pool_byte_regs[c].rnum;
                  if (!byte_free (verts[b], rnum))
                    continue;
                  bool dup = false;
                  for (int b2 = 0; b2 < b; ++b2)
                    if (rn[b2] == rnum)
                      dup = true;
                  if (!dup)
                    {
                      rn[b] = rnum;
                      found = true;
                    }
                }
              if (!found)
                ok = false;
            }
          if (ok)
            {
              commit (rn);
              placed = true;
            }
        }

      if (!placed)
        {
          for (int b = 0; b < size; ++b)
            sym->regs[b] = 0;
          spill_this (sym);
        }
    }

  return vreg;
}

/*------------------------------------------------------------------------
 * Per-iCode rMask/rSurv from live-range spans, mirroring the legacy
 * createRegMask() walk (no tree decomposition involved).  The span
 * [liveFrom, liveTo] over-approximates exact data-flow liveness on
 * branching paths, which only makes the save sets more conservative
 * (a superset); gen.c consumes them for the caller-save, callee_saves
 * and ISR machinery.
 *------------------------------------------------------------------------*/
static void
set_masks_from_live_ranges (ebbIndex *ebbi)
{
  for (int b = 0; b < ebbi->count; ++b)
    for (iCode *ic = ebbi->bbOrder[b]->sch; ic; ic = ic->next)
      {
        symbol *sym;
        int key;

        bitVectClear (ic->rMask);
        bitVectClear (ic->rSurv);

        for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
             sym = (symbol *) hTabNextItem (liveRanges, &key))
          {
            int k;

            if (!sym->nRegs || sym->isspilt)
              continue;
            if (sym->liveFrom > ic->seq || sym->liveTo < ic->seq)
              continue;

            for (k = 0; k < sym->nRegs; ++k)
              if (sym->regs[k])
                {
                  ic->rMask =
                    bitVectSetBit (ic->rMask, sym->regs[k]->rIdx);
                  if (sym->liveTo != ic->seq)
                    ic->rSurv =
                      bitVectSetBit (ic->rSurv, sym->regs[k]->rIdx);
                }
          }
      }
}

/*------------------------------------------------------------------------
 * Rematerialisation marking (focused replica of the legacy
 * packRegisters() rules that matter for pool admission: pointer iTemp
 * chains rooted at address-of-constant become rematerialisable instead
 * of spilling to RAM).  packRegisters() itself is port-static in
 * ralloc.c, so only its remat/uptr transformations are mirrored here.
 *------------------------------------------------------------------------*/
static void
pack_remat (eBBlock **ebbs, int count)
{
  for (int b = 0; b < count; ++b)
    for (iCode *ic = ebbs[b]->sch; ic; ic = ic->next)
      {
        /* address of a true sym: rematerialisable */
        if (ic->op == ADDRESS_OF &&
            IS_ITEMP (IC_RESULT (ic)) &&
            IS_TRUE_SYMOP (IC_LEFT (ic)) &&
            bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1 &&
            !OP_SYMBOL (IC_LEFT (ic))->onStack)
          {
            OP_SYMBOL (IC_RESULT (ic))->remat = 1;
            OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
            OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
          }

        /* straight assignment carries the remat flag */
        if (ic->op == '=' &&
            !POINTER_SET (ic) &&
            IS_SYMOP (IC_RIGHT (ic)) &&
            OP_SYMBOL (IC_RIGHT (ic))->remat &&
            !IS_CAST_ICODE (OP_SYMBOL (IC_RIGHT (ic))->rematiCode) &&
            !isOperandGlobal (IC_RESULT (ic)) &&
            bitVectnBitsOn (OP_SYMBOL (IC_RESULT (ic))->defs) <= 1 &&
            !OP_SYMBOL (IC_RESULT (ic))->addrtaken)
          {
            OP_SYMBOL (IC_RESULT (ic))->remat =
              OP_SYMBOL (IC_RIGHT (ic))->remat;
            OP_SYMBOL (IC_RESULT (ic))->rematiCode =
              OP_SYMBOL (IC_RIGHT (ic))->rematiCode;
          }

        /* pointer-to-pointer cast of a remat */
        if (ic->op == CAST &&
            IS_SYMOP (IC_RIGHT (ic)) &&
            OP_SYMBOL (IC_RIGHT (ic))->remat &&
            bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1 &&
            !OP_SYMBOL (IC_RESULT (ic))->addrtaken)
          {
            sym_link *to_type = operandType (IC_LEFT (ic));
            sym_link *from_type = operandType (IC_RIGHT (ic));
            if (IS_PTR (to_type) && IS_PTR (from_type))
              {
                OP_SYMBOL (IC_RESULT (ic))->remat = 1;
                OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
                OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
              }
          }

        /* +/- literal on a remat */
        if ((ic->op == '+' || ic->op == '-') &&
            IS_SYMOP (IC_LEFT (ic)) &&
            IS_ITEMP (IC_RESULT (ic)) &&
            IS_OP_LITERAL (IC_RIGHT (ic)) &&
            OP_SYMBOL (IC_LEFT (ic))->remat &&
            (!IS_SYMOP (IC_RIGHT (ic)) ||
             !IS_CAST_ICODE (OP_SYMBOL (IC_RIGHT (ic))->rematiCode)) &&
            bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1)
          {
            OP_SYMBOL (IC_RESULT (ic))->remat = 1;
            OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
            OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
          }

        /* pointer usage marks */
        if (POINTER_SET (ic) && IS_SYMOP (IC_RESULT (ic)))
          OP_SYMBOL (IC_RESULT (ic))->uptr = 1;
        if (POINTER_GET (ic) && IS_SYMOP (IC_LEFT (ic)))
          OP_SYMBOL (IC_LEFT (ic))->uptr = 1;

        /* R0/R1 pointer-scratch accounting (replica of the legacy
           packRegisters block): gen.c relies on mcs251_ptrRegReq to
           include ar0/ar1 in callee_saves save sets and elsewhere
           whenever the function addresses stack/idata/near-pointer
           operands through R0/R1. */
        if (!SKIP_IC2 (ic))
          {
            if (options.useXstack && ic->parmPush &&
                (ic->op == IPUSH || ic->op == IPOP))
              mcs251_ptrRegReq++;
            if (ic->op == IFX && IS_SYMOP (IC_COND (ic)))
              mcs251_ptrRegReq +=
                ((OP_SYMBOL (IC_COND (ic))->onStack ||
                  OP_SYMBOL (IC_COND (ic))->iaccess ||
                  SPEC_OCLS (OP_SYMBOL (IC_COND (ic))->etype) == idata)
                 ? 1 : 0);
            else if (ic->op == JUMPTABLE && IS_SYMOP (IC_JTCOND (ic)))
              mcs251_ptrRegReq +=
                ((OP_SYMBOL (IC_JTCOND (ic))->onStack ||
                  OP_SYMBOL (IC_JTCOND (ic))->iaccess ||
                  SPEC_OCLS (OP_SYMBOL (IC_JTCOND (ic))->etype) == idata)
                 ? 1 : 0);
            else
              {
                if (IS_SYMOP (IC_LEFT (ic)))
                  mcs251_ptrRegReq +=
                    ((OP_SYMBOL (IC_LEFT (ic))->onStack ||
                      OP_SYMBOL (IC_LEFT (ic))->iaccess ||
                      SPEC_OCLS (OP_SYMBOL (IC_LEFT (ic))->etype) == idata)
                     ? 1 : 0);
                if (IS_SYMOP (IC_RIGHT (ic)))
                  mcs251_ptrRegReq +=
                    ((OP_SYMBOL (IC_RIGHT (ic))->onStack ||
                      OP_SYMBOL (IC_RIGHT (ic))->iaccess ||
                      SPEC_OCLS (OP_SYMBOL (IC_RIGHT (ic))->etype) == idata)
                     ? 1 : 0);
                if (IS_SYMOP (IC_RESULT (ic)))
                  mcs251_ptrRegReq +=
                    ((OP_SYMBOL (IC_RESULT (ic))->onStack ||
                      OP_SYMBOL (IC_RESULT (ic))->iaccess ||
                      SPEC_OCLS (OP_SYMBOL (IC_RESULT (ic))->etype) == idata)
                     ? 1 : 0);
                if (POINTER_GET (ic) && IS_SYMOP (IC_LEFT (ic)) &&
                    getSize (OP_SYMBOL (IC_LEFT (ic))->type) <=
                      (unsigned int) NEARPTRSIZE)
                  mcs251_ptrRegReq++;
                if (POINTER_SET (ic) && IS_SYMOP (IC_RESULT (ic)) &&
                    getSize (OP_SYMBOL (IC_RESULT (ic))->type) <=
                      (unsigned int) NEARPTRSIZE)
                  mcs251_ptrRegReq++;
              }
          }
      }
}

/*------------------------------------------------------------------------
 * Live-range preparation: the legacy pipeline computes nRegs/regType
 * before allocation.  Set them for every iTemp live range (gen.c
 * relies on nRegs); mark only shapes the pool model admits as
 * candidates (for_newralloc drives create_cfg()).
 *------------------------------------------------------------------------*/
static void
prepare_live_ranges (ebbIndex *ebbi)
{
  symbol *sym;
  int key;

  recomputeLiveRanges (ebbi->bbOrder, ebbi->count, FALSE);

  /* Mirror the legacy serialRegAssign() pass that requests storage for
     true-symbol results (e.g. RECEIVE iCodes writing parameters into
     their slots); without it glue never emits their .ds space. */
  for (int b = 0; b < ebbi->count; ++b)
    for (iCode *ic = ebbi->bbOrder[b]->sch; ic; ic = ic->next)
      if (IC_RESULT (ic) && ic->op != IFX && IS_TRUE_SYMOP (IC_RESULT (ic)))
        OP_SYMBOL (IC_RESULT (ic))->allocreq++;

  for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
       sym = (symbol *) hTabNextItem (liveRanges, &key))
    {
      sym->for_newralloc = false;

      if (!sym->isitmp || sym->liveTo == sym->liveFrom)
        continue;

      const int size = getSize (sym->type);

      mcs251_ralloc2::operand_kind kind = mcs251_ralloc2::kind_plain;
      if (IS_AGGREGATE (sym->type))
        kind = mcs251_ralloc2::kind_aggregate;
      else if (sym->isptr || IS_PTR (sym->type))
        kind = mcs251_ralloc2::kind_pointer;
      else if (IS_BITVAR (sym->etype))
        kind = mcs251_ralloc2::kind_bit;

      if (kind == mcs251_ralloc2::kind_bit)
        sym->regType = REG_BIT;
      else
        sym->regType = REG_GPR;

      /* gen.c only considers rematerialisation (and spill locations)
         for symbols with isspilt || nRegs == 0, mirroring the legacy
         regTypeNum() convention: only register candidates carry a
         nonzero nRegs. */
      sym->nRegs = 0;

      if (mcs251_ralloc2::admit_operand (size, kind) ==
          mcs251_ralloc2::admit_in_reg)
        {
          sym->nRegs = size;
          sym->for_newralloc = true;
        }
    }
}

/* Spill every iTemp the pool model does not admit, mirroring the
   legacy "assigned or spilt" invariant. */
static void
spill_unadmitted (void)
{
  symbol *sym;
  int key;

  for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
       sym = (symbol *) hTabNextItem (liveRanges, &key))
    if (sym->isitmp && !sym->for_newralloc && !sym->remat &&
        !sym->accuse && !sym->ruonly && !sym->isspilt &&
        sym->liveTo != sym->liveFrom && !sym->usl.spillLoc)
      spill_this (sym);
}

/*------------------------------------------------------------------------*/
/* Entry point: shared framework graphs, conflict-graph assignment,      */
/* liveness-derived masks; returns the iCode chain for gen.               */
/*------------------------------------------------------------------------*/
iCode *
mcs251_ralloc2_cc (ebbIndex *ebbi)
{
  if (!ebbi || !port)
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__,
              "invalid mcs251 ralloc2 configuration");
      exit (1);
    }

  if (!mcs251_ralloc2::assignment_selection_enabled ())
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__,
              "mcs251 ralloc2: assignment selection disabled");
      exit (1);
    }

  /* Architecture (fixed by the MT-1C review decision): this port
     uses the shared framework for CFG + byte-level conflict-graph
     construction only; register selection is the port-side greedy
     over that graph.  No tree decomposition is computed here. */
  cfg_t control_flow_graph;
  con_t conflict_graph;

  iCode *ic = create_cfg (control_flow_graph, conflict_graph, ebbi);

  ralloc2_assign (conflict_graph);
  set_masks_from_live_ranges (ebbi);

  /* Legacy tail piece: stacked automatic variable offsets must be
     redone after allocation (callee_saves prologue pushes shift the
     frame). */
  if (currFunc)
    redoStackOffsets ();

  /* Legacy tail piece: when the function may address through R0/R1,
     they must enter regsUsed so the callee_saves prologue pushes the
     pair that the epilogue pops. */
  if (currFunc && mcs251_ptrRegReq)
    {
      currFunc->regsUsed =
        bitVectSetBit (currFunc->regsUsed, R0_IDX);
      currFunc->regsUsed =
        bitVectSetBit (currFunc->regsUsed, R1_IDX);
    }

  /* Safety net: any candidate that never appeared as a conflict vertex
     must still be assigned-or-spilt before gen runs. */
  {
    symbol *sym;
    int key;
    for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
         sym = (symbol *) hTabNextItem (liveRanges, &key))
      if (sym->for_newralloc && !sym->isspilt)
        {
          bool complete = true;
          for (int b = 0; b < sym->nRegs; ++b)
            if (!sym->regs[b])
              complete = false;
          if (!complete)
            {
              for (int b = 0; b < sym->nRegs; ++b)
                sym->regs[b] = 0;
              spill_this (sym);
            }
        }
  }

  return ic;
}

/* Directed-test adapter.  MT-1C still does not install this in
   mcs251_port: production continues to use mcs251_assignRegisters in
   ralloc.c.  The gate links a temporary compiler whose port table
   names this adapter (tests/check-ralloc2-directed.py). */
void
mcs251_ralloc2_assignRegisters (ebbIndex *ebbi)
{
  eBBlock **ebbs = ebbi->bbOrder;
  const int count = ebbi->count;
  iCode *ic;

  port->num_regs = MCS251_ALLOC_REG_COUNT;
  mcs251_ralloc2_sloc_num = 0;
  mcs251_ptrRegReq = 0;

  pack_remat (ebbs, count);
  prepare_live_ranges (ebbi);
  spill_unadmitted ();

  ic = mcs251_ralloc2_cc (ebbi);

  ic = iCodeLabelOptimize (ic);
  if (optimize.genconstprop)
    recomputeValinfos (ic, ebbi, "_3");

  doOverlays (ebbs, count);

  mcs251_genCode (ic);
}

#else /* MCS251_RALLOC2_STANDALONE_TEST */

/*------------------------------------------------------------------------*/
/* Standalone self test of the descriptor/constraint core.               */
/*------------------------------------------------------------------------*/

#include <cstdio>
#include <cstdlib>

using namespace mcs251_ralloc2;

static int failures = 0;

#define CHECK(cond, msg)                                        \
  do                                                            \
    {                                                           \
      if (!(cond))                                              \
        {                                                       \
          std::printf ("FAIL: %s (%s:%d)\n", msg, __FILE__,     \
                       __LINE__);                               \
          ++failures;                                           \
        }                                                       \
    }                                                           \
  while (0)

int
main (void)
{
  /* Register pool shape. */
  CHECK (kPoolByteRegs == 14, "pool covers R0..R9 + R12..R15");
  for (int i = 0; i < kPoolByteRegs; ++i)
    {
      CHECK (byte_rnum_in_pool (pool_byte_regs[i].rnum),
             "pool entry inside pool range");
      CHECK (pool_byte_regs[i].rnum != 10 &&
             pool_byte_regs[i].rnum != 11,
             "R10/R11 (B/ACC aliases) never allocatable");
      CHECK (pool_byte_regs[i].rnum < 16, "R16..R31 stay closed");
    }
  CHECK (!byte_rnum_in_pool (10) && !byte_rnum_in_pool (11),
        "R10/R11 rejected by the membership predicate");
  CHECK (!byte_rnum_in_pool (16) && !byte_rnum_in_pool (31),
        "R16..R31 rejected by the membership predicate");
  CHECK (kPoolByteRegs > kMaxFrameworkRegs,
        "pool exceeds the framework bound; port-side assignment is the "
        "documented workaround");

  /* WR/DR overlap model. */
  for (int i = 0; i < 7; ++i)
    CHECK (word_start_legal (word_starts[i]), "legacy word start legal");
  CHECK (!word_start_legal (10), "R10 word tuple impossible");
  CHECK (word_start_legal (8) && word_start_legal (12) &&
         word_start_legal (14),
         "full legacy word set includes R8/R12/R14 pairs");
  for (int i = 0; i < 3; ++i)
    CHECK (dword_start_legal (dword_starts[i]),
           "legacy dword start legal");
  CHECK (!dword_start_legal (8), "DR8 impossible (R10/R11)");
  CHECK (dword_start_legal (0) && dword_start_legal (4) &&
         dword_start_legal (12), "dword set is DR0/DR4/DR12");

  /* Clobber model. */
  CHECK (byte_clobber (8) == clobber_native_mul &&
         byte_clobber (15) == clobber_native_mul,
         "R8..R15 clobbered by native mul");
  CHECK (byte_clobber (7) == clobber_none,
         "bank registers untouched by native mul");
  CHECK (fixed_temporal_clobber (20) == clobber_call &&
         fixed_temporal_clobber (24) == clobber_call &&
         fixed_temporal_clobber (28) == clobber_call,
         "DR20/DR24/DR28 classified as call clobber");
  CHECK (fixed_temporal_clobber (10) == clobber_call &&
         fixed_temporal_clobber (11) == clobber_call,
         "B/ACC classified as call clobber");

  /* Admission matrix. */
  const operand_kind kinds[5] = { kind_plain, kind_pointer, kind_bit,
                                  kind_aggregate, kind_float };
  for (int size = -1; size <= 9; ++size)
    for (int k = 0; k < 5; ++k)
      {
        const admit_decision d = admit_operand (size, kinds[k]);
        if (d == admit_in_reg)
          CHECK ((kinds[k] == kind_plain || kinds[k] == kind_float) &&
                 size >= 1 && size <= 4,
                 "in_reg only for plain/float scalars of 1-4 bytes");
        if (size <= 0 || size > 8)
          CHECK (d == admit_fail, "impossible sizes fail closed");
        else
          CHECK (d != admit_fail,
                 "expressible sizes never fail (spill instead)");
      }

  /* Cost model. */
  CHECK (tuple_cost (6) < tuple_cost (4) && tuple_cost (4) < tuple_cost (2) &&
         tuple_cost (2) < tuple_cost (14) && tuple_cost (14) < tuple_cost (12) &&
         tuple_cost (12) < tuple_cost (8) && tuple_cost (8) < tuple_cost (0),
         "tuple preference mirrors legacy order");

  /* Assignment selection is enabled in MT-1C. */
  CHECK (assignment_selection_enabled (), "assignment selection enabled");

  if (failures)
    {
      std::printf ("ralloc2 self test: %d failure(s)\n", failures);
      return 1;
    }
  std::printf ("PASS: ralloc2 core (14-slot pool, WR/DR overlap, "
               "clobber classes, admission matrix, costs)\n");
  return 0;
}

#endif /* MCS251_RALLOC2_STANDALONE_TEST */
