/*------------------------------------------------------------------------

  ralloc2.cc - register allocator for the MCS-251 port (MT-1C model;
  MT-1E Phase 2B unique production path)

  Reuses SDCC's shared register-allocation framework
  (SDCCralloc.hpp / SDCCsalloc.hpp): the control-flow graph, the
  byte-level conflict graph and the liveness data all come from
  create_cfg().  Assignment is a port-side greedy walk over that shared
  graph; no second allocator framework or tree-decomposition path is
  introduced.

  Scope of the allocator model (roadmap MT-1C):
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

  MT-1D installed the ralloc2 adapter as the default MCS-251 port callback.
  MT-1E Phase 1 Classes 1-5 and Phase 2A closed the production fail-closed
  boundary in stages.  Phase 2B removes the retained allocator and keeps
  directed disabled-capability mutations fail-closed.

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

#include <set>

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

#ifdef MCS251_RALLOC2_TRACE_SELECTION
#include <stdio.h>
#endif

namespace mcs251_ralloc2 {

/* Compile-time bound of the shared framework (SDCCralloc.hpp).  Duplicated
   as a plain constant so the standalone unit test can reason about it
   without SDCC headers. */
static const int kMaxFrameworkRegs = 9;

/* Full legacy byte pool: R0..R9 + R12..R15 (MT-1A facts).  R10/R11 are
   permanently unallocatable (B/ACC aliases); R16..R31 are closed. */
static const int kPoolByteRegs = 14;
static const int kTargetPointerBytes = 3;

struct byte_reg_desc
{
  const char *name;
  int rnum;            /* architectural register number 0..15 */
  bool ptr_capable;    /* legacy REG_PTR designation (R0/R1) */
};

/* Historical allocation-table order (the former serial allocator's register table): R7..R0,
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

/* MCS-251 pointers are three bytes.  The legacy allocator admitted that
   width only to the banked byte registers R0..R7; treating all fourteen pool
   bytes as interchangeable let a live pointer straddle the bank boundary
   (for example R1:R0:R15), which breaks generic-pointer lowering.  This is
   intentionally a pointer-width rule, not a wholesale legacy tuple policy:
   ralloc2's 2/4/5..8-byte scalar layouts are required by the closed Class-5
   pressure path and are supported by gen.c. */
#ifndef MCS251_RALLOC2_STANDALONE_TEST
static bool
mcs251_byte_layout_allows (const symbol *sym, int rnum)
{
#ifdef MCS251_RALLOC2_DISABLE_POINTER_BANK_LAYOUT
  (void) sym;
  (void) rnum;
  return true;
#else
  return sym->nRegs != kTargetPointerBytes ||
    rnum < 8;
#endif
}
#endif

/* WR/DR overlap model (MT-1A facts): a word is an even-start pair of
   consecutive byte registers, MSB at the lower number; a dword is a
   4-aligned run of four.  Preference order mirrors the legacy
   candidate lists.  No second physical register file exists. */
static const int word_starts[7] = { 6, 4, 2, 14, 12, 8, 0 };
static const int dword_starts[3] = { 4, 0, 12 };

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

/* Operand admission: ABI-sized plain/float scalars and target pointers may
   live in the pool; aggregate temporaries are pointer-decayed before this
   decision and therefore use the same rule.  Bit values retain the separate
   b0..b7 allocator.  Five-to-eight-byte ordinary values are lower priority
   than CALL/PCALL results in ralloc2_assign(), so pressure preserves the
   return-value capture invariant. */
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
  if (size <= 0)
    return admit_fail;

  switch (kind)
    {
      case kind_plain:
      case kind_float:
#ifdef MCS251_RALLOC2_DISABLE_WIDE_SCALAR_ADMISSION
        if (size <= 4)
#else
        if (size <= 8)
#endif
          return admit_in_reg;
        return admit_spill;
      case kind_pointer:
#ifdef MCS251_RALLOC2_DISABLE_POINTER_ADMISSION
        return admit_spill;
#else
        return size <= kTargetPointerBytes ? admit_in_reg : admit_spill;
#endif
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
        ic->op == GET_VALUE_AT_ADDRESS ||
        (ic->op == '=' && !POINTER_SET (ic))))
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
          /* Keep the legacy byte-order rule for destructive arithmetic:
             same-offset bytes may alias for native word forms.  Plain
             assignment needs the same low-to-high overlap guard in every
             mode now that Phase 2A admits multi-byte pointer temporaries;
             otherwise a cast/copy tuple can overwrite a later source byte
             (qsort --model-small --nostdinc, genAssign assertion). */
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
static std::vector<symbol *> mcs251_ralloc2_spill_slots;
static std::set<std::pair<int, int> > mcs251_ralloc2_symbol_conflicts;

static std::pair<int, int>
ordered_symbol_pair (int a, int b)
{
  return a < b ? std::make_pair (a, b) : std::make_pair (b, a);
}

/* Share a spill slot only when every stored value is known dead.  Legacy
   noOverLap() uses symbol::clashes, but ralloc2 spills some unadmitted values
   before create_cfg() has populated that field.  In that case the linear
   live interval is a conservative fallback: strict separation is sufficient
   even though it cannot exploit mutually exclusive CFG paths. */
static bool
spill_slot_can_hold (symbol *sloc, symbol *sym)
{
  symbol *occupant;

  if (getSize (sloc->type) < getSize (sym->type) ||
      (IS_BIT (sloc->type) != IS_BIT (sym->type)))
    return false;

  for (occupant = (symbol *) setFirstItem (sloc->usl.itmpStack); occupant;
       occupant = (symbol *) setNextItem (sloc->usl.itmpStack))
    {
      if (occupant->for_newralloc && sym->for_newralloc)
        {
          if (mcs251_ralloc2_symbol_conflicts.count (
                ordered_symbol_pair (occupant->key, sym->key)))
            return false;
        }
      else if (occupant->clashes)
        {
          if (bitVectBitValue (occupant->clashes, sym->key))
            return false;
        }
      else if (!(occupant->liveTo < sym->liveFrom ||
                 sym->liveTo < occupant->liveFrom))
        return false;
    }
  return true;
}

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

  /* isOperandVolatile() ignores iTemp volatility by design (it returns
     0 for iTemps when chkTemp is FALSE), but a spilled iTemp
     materialises as a DIRECT aop whose aop_is_volatile comes from
     isVolatile(type): the top-level DCL_PTR_VOLATILE for pointers or
     SPEC_VOLATILE for scalars.  A volatile-typed iTemp (e.g. a pointer
     value loaded from a volatile pointer variable) would then trip
     opPut's consistency assert (gen_lower.c.inc, review round 1 P1).
     Strip the qualifier at every link of a private copy so both sides
     agree; never mutate the shared chain. */
  if (SPEC_VOLATILE (sym->etype) ||
      (!IS_SPEC (sym->type) && DCL_PTR_VOLATILE (sym->type)))
    {
      sym->type = copyLinkChain (sym->type);
      sym->etype = getSpec (sym->type);
      for (sym_link *sl = sym->type; sl; sl = sl->next)
        if (IS_SPEC (sl))
          SPEC_VOLATILE (sl) = 0;
        else
          DCL_PTR_VOLATILE (sl) = 0;
    }

  if (sym->remat)
    {
      /* gen.c treats nRegs == 0 as the rematerialised/spill path. */
      sym->nRegs = 0;
      return;
    }

  if (sym->usl.spillLoc)
    {
      /* A spill location already exists (left by earlier pipeline
         stages).  Adopt it instead of leaving the symbol in a
         half-spilt state that would break the assigned-or-spilt
         invariant expected by gen.c. */
      sym->isspilt = sym->spillA = 1;
      if (!sym->remat)
        sym->usl.spillLoc->allocreq++;
      sym->nRegs = 0;
      return;
    }

#ifndef MCS251_RALLOC2_DISABLE_SPILL_SLOT_REUSE
  for (symbol *candidate : mcs251_ralloc2_spill_slots)
    if (spill_slot_can_hold (candidate, sym))
      {
        sym->usl.spillLoc = candidate;
        sym->isspilt = sym->spillA = sym->stackSpil = 1;
        candidate->allocreq++;
        addSetHead (&candidate->usl.itmpStack, sym);
        sym->nRegs = 0;
        return;
      }
#endif

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
  mcs251_ralloc2_spill_slots.push_back (sloc);
  sym->nRegs = 0;
}

/*------------------------------------------------------------------------
 * Native-mul result exclusion (matching historical allocation semantics): results
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

/* A call result is returned in DPL/DPH/B/ACC/R4..R7 and cannot use the
   ordinary spill path: gen.c does not materialise a spilled CALL/PCALL
   result before restoring caller-save registers.  Keep these live ranges
   ahead of ordinary temporaries in the greedy pass so pressure is resolved
   by spilling a materialisable operand instead of silently dropping the
   return value. */
static bool
is_call_result (const symbol *sym)
{
  int key;

  if (!sym || !sym->defs)
    return false;

  for (key = 0; key < sym->defs->size; ++key)
    if (bitVectBitValue (sym->defs, key))
      {
        iCode *ic = (iCode *) hTabItemWithKey (iCodehTab, key);
        if (ic && (ic->op == CALL || ic->op == PCALL))
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

  /* Reduce the byte graph to symbol pairs once.  The absence of such a
     pair proves that every byte of the two live ranges may share storage;
     spill_this() uses this after register placement fails. */
  {
    typename boost::graph_traits<I_t>::edge_iterator ei, ee;
    for (boost::tie (ei, ee) = boost::edges (I); ei != ee; ++ei)
      {
        const int a = I[boost::source (*ei, I)].v;
        const int b = I[boost::target (*ei, I)].v;
        if (a != b)
          mcs251_ralloc2_symbol_conflicts.insert (ordered_symbol_pair (a, b));
      }
  }

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
                      {
#ifndef MCS251_RALLOC2_DISABLE_CALL_RESULT_PRIORITY
                        const bool ac = is_call_result (order_syms[a]);
                        const bool bc = is_call_result (order_syms[b]);
                        if (ac != bc)
                          return ac;
#endif
                        return order_syms[a]->liveFrom <
                               order_syms[b]->liveFrom;
                      });
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

      const bool needs_ptr_reg = sym->regType == REG_PTR;
      auto byte_free = [&] (int vertex, int rnum)
      {
        if (!mcs251_ralloc2::byte_rnum_in_pool (rnum) ||
            !mcs251_ralloc2::mcs251_byte_layout_allows (sym, rnum) ||
            (needs_ptr_reg ? rnum > 1 :
             (mcs251_ptrRegReq && rnum <= 1)) ||
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
          /* gen.c's call-result move consumes the ABI return tuple
             DPL/DPH/B/A.  For two-byte CALL/PCALL results, start at R7:R6
             so the generator's R0/R1 indirect-pointer pair remains free;
             the remaining starts retain the legal legacy tuple set.  This
             avoids forcing a later stack-argument load to save the just-
             returned value a second time. */
          static const int call_word_starts[7] = { 6, 4, 2, 0, 14, 12, 8 };
          static const int call_dword_starts[3] = { 0, 4, 12 };
          const int nstarts = size == 2 ? 7 : 3;
          const int *starts;
          if (is_call_result (sym))
            starts = size == 2 ? call_word_starts : call_dword_starts;
          else
            starts = size == 2 ? mcs251_ralloc2::word_starts
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

  /* Mirror the former serial register-assignment pass that requests storage for
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

      if (sym->liveTo == sym->liveFrom)
        continue;

      if (!sym->isitmp)
        {
          /* Legacy regTypeNum() gives true symbols no registers: they
             stay memory-resident and their storage comes from the
             allocreq pass above; gen.c never gave them pool
             registers.  Set it explicitly so aggregate/bit user
             symbols are not left in a half state. */
          sym->nRegs = 0;
          continue;
        }

      /* Legacy regTypeNum() keeps packRegisters' condition-marked and
         accumulator/return-only temporaries out of register
         allocation: a compare result consumed immediately by a single
         IFX carries REG_CND (carry convention) and a byte POINTER_GET
         result consumed the same way carries accuse (accumulator
         convention).  Giving those shapes pool registers trips a gen.c
         internal error on if (*gp) probes (review round 1,
         gen_lower.c.inc:2365), so preserve the legacy state exactly:
         no registers, no spill. */
      if (sym->regType == REG_CND)
        continue;

      if (sym->ruonly || sym->accuse)
        {
          if (IS_AGGREGATE (sym->type) || sym->isptr)
            sym->type = aggrToPtr (sym->type, FALSE);
          else if (IS_BIT (sym->type))
            sym->regType = REG_CND;
          continue;
        }

      /* Mirror the legacy regTypeNum() type convention before any
         admission decision: aggregate and pointer-holding temporaries
         decay to pointer type (aggrToPtr), the only shape gen.c has
         ever observed for them (the former allocator applied the same conversion
         in its register and no-register branches). */
      if (IS_AGGREGATE (sym->type) || sym->isptr)
        sym->type = aggrToPtr (sym->type, FALSE);

      const int size = getSize (sym->type);

      mcs251_ralloc2::operand_kind kind = mcs251_ralloc2::kind_plain;
      if (IS_AGGREGATE (sym->type))
        kind = mcs251_ralloc2::kind_aggregate;
      else if (sym->isptr || IS_PTR (sym->type))
        kind = mcs251_ralloc2::kind_pointer;
      else if (IS_BIT (sym->type))
        kind = mcs251_ralloc2::kind_bit;

      /* Bitfield member temporaries are NOT bit registers: legacy
         isFlagVar() only classifies true __bit/_Bool shapes as REG_BIT;
         unsigned-int-bitfield temps get plain GPR registers by size,
         and gen.c's wider-than-byte bitfield extraction is only proven
         for that legacy shape. */

      if (kind == mcs251_ralloc2::kind_bit)
        sym->regType = REG_BIT;
      else
        sym->regType = REG_GPR;

      /* gen.c only considers rematerialisation (and spill locations)
         for symbols with isspilt || nRegs == 0, mirroring the legacy
         regTypeNum() convention: only register candidates carry a
         nonzero nRegs. */
      sym->nRegs = 0;

      const mcs251_ralloc2::admit_decision admission =
        mcs251_ralloc2::admit_operand (size, kind);
      if (kind == mcs251_ralloc2::kind_pointer && size == 1 && sym->uptr)
        sym->regType = REG_PTR;

      /* genPointerGet's dual post-increment form has a proven stack-backed
         write-back path for --stack-auto.  A generic-pointer source iTemp
         held in GPRs is reloaded on every loop iteration after DPX was
         incremented, so its increment is lost (memccpy).  Keep the
         allocator selected but spill that narrow shape.  Near/far/data
         pointer temporaries use different generator paths; spilling those
         broadly breaks their address-space-specific register protocol. */
      const bool stack_auto_pointer_temp =
        (options.stackAuto ||
         (currFunc && IFFUNC_ISREENT (currFunc->type))) &&
        kind == mcs251_ralloc2::kind_pointer && IS_GENPTR (sym->type);

      if (((admission == mcs251_ralloc2::admit_in_reg &&
            !stack_auto_pointer_temp) ||
           (is_call_result (sym) && size <= 8)) && !sym->remat)
        {
          /* Rematerialisable temporaries never take registers: gen.c
             folds their uses into direct constant loads (the legacy
             shape); a registerised remat temp loses its materialising
             store and sends stale register bytes (found by the
             class-3 call-result probe). */
          sym->nRegs = size;
          sym->for_newralloc = true;
        }
    }
}

/* Spill every iTemp the pool model does not admit, mirroring the
   legacy "assigned or spilt" invariant.  Bit temporaries registered by
   assign_bit_registers keep their b-register, and REG_CND temporaries
   keep their legacy carry convention; neither is a spill.  A temporary
   whose storage packRegisters already pre-assigned (sir packing points
   spillLoc at the true symbol) flows into spill_this's adopt branch:
   without adoption the true symbol never gets allocreq, its storage is
   never emitted and dereferences through it read a nonexistent address
   (review round 2: dptr,#0x0000 on a runtime-copied local pointer). */
static void
spill_unadmitted (void)
{
  symbol *sym;
  int key;

  for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
       sym = (symbol *) hTabNextItem (liveRanges, &key))
    if (sym->isitmp && !sym->for_newralloc && !sym->nRegs && !sym->remat &&
        sym->regType != REG_CND &&
        !sym->accuse && !sym->ruonly && !sym->isspilt &&
        sym->liveTo != sym->liveFrom
#ifdef MCS251_RALLOC2_NO_SIR_ADOPT
        /* Mutation hook for the directed gate: skipping pre-assigned
           storage leaves the sir-packed true symbol unmaterialised and
           the ptrvar probe dereferences address 0. */
        && !sym->usl.spillLoc
#endif
      )
      spill_this (sym);
}

/*------------------------------------------------------------------------
 * Bit-valued temporaries: mirror the legacy getRegBit() policy.  The
 * eight bit registers b0..b7 are assigned greedily by live-range
 * overlap (first free lane, sorted by liveFrom); only when all eight
 * lanes are busy does the temporary spill to bit space.  gen.c's bit
 * test/assign paths are proven for the registerised shape; a spilt
 * bit temporary consumed by an if-test miscompiles (found by the
 * MT-1E directed fixture), so the legacy shape is preserved.
 *------------------------------------------------------------------------*/
static void
assign_bit_registers (void)
{
  symbol *sym;
  int key;
  std::vector<symbol *> bits;

  for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
       sym = (symbol *) hTabNextItem (liveRanges, &key))
    if (sym->isitmp && sym->regType == REG_BIT &&
        sym->liveTo != sym->liveFrom && !sym->ruonly && !sym->accuse &&
        !sym->remat && !sym->isspilt && !sym->nRegs)
      bits.push_back (sym);

  std::stable_sort (bits.begin (), bits.end (),
                    [] (const symbol *a, const symbol *b)
                    { return a->liveFrom < b->liveFrom; });

  int lane_last_to[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
  for (symbol *s : bits)
    {
      int chosen = -1;
      for (int b = 0; b < 8 && chosen < 0; ++b)
        if (lane_last_to[b] < s->liveFrom)
          chosen = b;
      if (chosen < 0)
        {
          spill_this (s);
          continue;
        }
      s->regs[0] = mcs251_regWithIdx (B0_IDX + chosen);
      s->nRegs = 1;
      /* SDCCglue emits BIT_BANK and the b0..b7 aliases only when this
         process-wide marker is set.  The legacy allocator sets it while
         accounting for allocated REG_BIT registers; ralloc2 must preserve
         the same glue contract. */
      BitBankUsed = 1;
      lane_last_to[chosen] = s->liveTo;
      if (currFunc)
        currFunc->regsUsed =
          bitVectSetBit (currFunc->regsUsed, B0_IDX + chosen);
    }
}

#ifdef MCS251_RALLOC2_DISABLE_PRESSURE_SUPPORT
/* Return the width seen by prepare_live_ranges().  Aggregate temporaries
   decay to pointers before admission, so their source object size must not
   trigger the retained-allocator guard.  GPTRSIZE is conservative for near
   aggregates and avoids allocating a throw-away sym_link just to query it. */
static int
ralloc2_value_size (const symbol *sym)
{
  return (IS_AGGREGATE (sym->type) || sym->isptr) ? GPTRSIZE
                                                  : getSize (sym->type);
}

static bool
operand_needs_pointer_register (const operand *op)
{
  return op && IS_SYMOP (op) &&
         (OP_SYMBOL_CONST (op)->onStack ||
          OP_SYMBOL_CONST (op)->iaccess ||
          SPEC_OCLS (OP_SYMBOL_CONST (op)->etype) == idata);
}
#endif

#ifdef MCS251_RALLOC2_DISABLE_GENERIC_CAST_SUPPORT
static bool
is_aggregate_pointer (sym_link *type)
{
  return type && IS_PTR (type) && type->next && IS_AGGREGATE (type->next);
}

static bool
is_void_pointer (sym_link *type)
{
  return type && IS_PTR (type) && type->next && IS_VOID (type->next);
}
#endif

#ifdef MCS251_RALLOC2_DISABLE_PDATA_SUPPORT
static bool
pointer_access_is_pdata (iCode *ic)
{
  const operand *op = POINTER_GET (ic) ? IC_LEFT (ic) : IC_RESULT (ic);
  sym_link *type = op ? operandType (op) : NULL;
  return type && IS_PTR (type) && DCL_TYPE (type) == PPOINTER;
}
#endif

#ifdef MCS251_RALLOC2_DISABLE_PRESSURE_SUPPORT
/* packRegisters() computes mcs251_ptrRegReq after the production fallback
   decision.  Mirror its read-only input test here so CALL-result capacity
   uses the same effective pool: R0/R1 are unavailable whenever the function
   needs an indirect-data pointer pair.  A conservative true only falls back
   a class-5 pressure shape; a false negative can silently lose a return. */
static bool
ralloc2_needs_pointer_registers (ebbIndex *ebbi)
{
  for (int b = 0; b < ebbi->count; ++b)
    for (iCode *ic = ebbi->bbOrder[b]->sch; ic; ic = ic->next)
      {
        if (SKIP_IC2 (ic))
          continue;

        if (options.useXstack && ic->parmPush &&
            (ic->op == IPUSH || ic->op == IPOP))
          return true;

        if (ic->op == IFX)
          {
            if (operand_needs_pointer_register (IC_COND (ic)))
              return true;
          }
        else if (ic->op == JUMPTABLE)
          {
            if (operand_needs_pointer_register (IC_JTCOND (ic)))
              return true;
          }
        else if (operand_needs_pointer_register (IC_LEFT (ic)) ||
                 operand_needs_pointer_register (IC_RIGHT (ic)) ||
                 operand_needs_pointer_register (IC_RESULT (ic)))
          return true;

        if (POINTER_GET (ic) && IS_SYMOP (IC_LEFT (ic)) &&
            getSize (OP_SYMBOL (IC_LEFT (ic))->type) <= NEARPTRSIZE)
          return true;
        if (POINTER_SET (ic) && IS_SYMOP (IC_RESULT (ic)) &&
            getSize (OP_SYMBOL (IC_RESULT (ic))->type) <= NEARPTRSIZE)
          return true;
      }

  return false;
}
#endif

/* Phase 2A admits every remaining production shape.  The conditionals below
   exist only as directed-test mutations.  They now reject the mutated build
   fail-closed because Phase 2B has removed the legacy fallback target. */
static bool
ralloc2_test_mutation_rejects (ebbIndex *ebbi)
{
#ifdef MCS251_RALLOC2_DISABLE_PRESSURE_SUPPORT
  int pressure = 0;
  const int call_result_capacity =
    mcs251_ralloc2::kPoolByteRegs -
    (ralloc2_needs_pointer_registers (ebbi) ? 2 : 0);
  std::vector<symbol *> call_results;
  symbol *sym;
  int key;
#endif

#ifdef MCS251_RALLOC2_DISABLE_STACK_AUTO_SUPPORT
  if (options.stackAuto)
    return true;
#endif
#ifdef MCS251_RALLOC2_DISABLE_REENTRANT_SUPPORT
  if (currFunc && IFFUNC_ISREENT (currFunc->type))
    return true;
#endif
#ifdef MCS251_RALLOC2_DISABLE_ISR_SUPPORT
  if (currFunc && IFFUNC_ISISR (currFunc->type))
    return true;
#endif

#if defined (MCS251_RALLOC2_DISABLE_JUMPTABLE_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_INLINEASM_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_RECEIVE_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_PDATA_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_STACK_ADDRESS_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_GENERIC_CAST_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_POINTER_COMPARE_SUPPORT)
  for (int b = 0; b < ebbi->count; ++b)
    for (iCode *ic = ebbi->bbOrder[b]->sch; ic; ic = ic->next)
      switch (ic->op)
        {
#ifdef MCS251_RALLOC2_DISABLE_JUMPTABLE_SUPPORT
        case JUMPTABLE:
#endif
#ifdef MCS251_RALLOC2_DISABLE_INLINEASM_SUPPORT
        case INLINEASM:
#endif
#if defined (MCS251_RALLOC2_DISABLE_JUMPTABLE_SUPPORT) || \
    defined (MCS251_RALLOC2_DISABLE_INLINEASM_SUPPORT)
          return true;
#endif
#ifdef MCS251_RALLOC2_DISABLE_RECEIVE_SUPPORT
        /* Directed mutation: restore the pre-class-3 RECEIVE fallback.
           Production never defines this macro; the route gate must fail if
           a callee carrying parameters silently returns to legacy. */
        case RECEIVE:
          return true;
#endif
        default:
#ifdef MCS251_RALLOC2_DISABLE_PDATA_SUPPORT
          if (options.model == MODEL_LARGE &&
              (POINTER_GET (ic) || POINTER_SET (ic)) &&
              pointer_access_is_pdata (ic))
            return true;
#endif
#ifdef MCS251_RALLOC2_DISABLE_STACK_ADDRESS_SUPPORT
          if (options.stackAuto && ic->op == ADDRESS_OF &&
              isOperandOnStack (IC_LEFT (ic)))
            return true;
#endif
#ifdef MCS251_RALLOC2_DISABLE_GENERIC_CAST_SUPPORT
          if (ic->op == CAST)
            {
              const operand *cast_src = IC_RIGHT (ic);
              const operand *cast_dst = IC_RESULT (ic);

              /* Wide aggregate copies lower their source/destination to an
                 aggregate pointer and cast it to memcpy's generic void *.
                 Admit only that exact pointer-to-void shape.  Other generic
                 pointer casts (including aggregate-field reinterpretation)
                 remain unproved and keep the PNVI regression fail-closed. */
              if ((cast_src && IS_GENPTR (operandType (cast_src))) ||
                  (cast_dst && IS_GENPTR (operandType (cast_dst))))
                {
                  sym_link *src_type = operandType (cast_src);
                  sym_link *dst_type = operandType (cast_dst);
                  const bool aggregate_copy_cast =
                    (is_aggregate_pointer (src_type) &&
                     is_void_pointer (dst_type)) ||
                    (is_void_pointer (src_type) &&
                     is_aggregate_pointer (dst_type));

                  if (!aggregate_copy_cast)
                    return true;
                }
            }
#endif
#ifdef MCS251_RALLOC2_DISABLE_POINTER_COMPARE_SUPPORT
          if ((ic->op == EQ_OP || ic->op == NE_OP ||
               ic->op == '<' || ic->op == '>' ||
               ic->op == LE_OP || ic->op == GE_OP))
            {
              const operand *cmp_l = IC_LEFT (ic);
              const operand *cmp_r = IC_RIGHT (ic);

              if ((cmp_l && IS_SYMOP (cmp_l) &&
                   (IS_PTR (operandType (cmp_l)) ||
                    OP_SYMBOL_CONST (cmp_l)->isptr)) ||
                  (cmp_r && IS_SYMOP (cmp_r) &&
                   (IS_PTR (operandType (cmp_r)) ||
                    OP_SYMBOL_CONST (cmp_r)->isptr)))
                return true;
            }
#endif
          break;
        }
#endif

#ifdef MCS251_RALLOC2_DISABLE_PRESSURE_SUPPORT
  for (sym = (symbol *) hTabFirstItem (liveRanges, &key); sym;
       sym = (symbol *) hTabNextItem (liveRanges, &key))
    {
      if (sym->isitmp && sym->liveTo != sym->liveFrom)
      {
        const int size = ralloc2_value_size (sym);

        /* The ABI returns scalars up to eight bytes in registers.  Values in
           that window are admitted by prepare_live_ranges(); CALL/PCALL
           results sort ahead of ordinary values so gen.c captures them
           before restoring caller-save registers.  Wider ordinary values
           are represented by materialised spill slots.  Aggregate
           temporaries are pointer-decayed by prepare_live_ranges() and
           therefore use their pointer width here. */
        if (size > 8)
          return true;

        if (is_call_result (sym))
          call_results.push_back (sym);

        pressure += size;
      }
    }

  /* genCall() cannot materialise an ordinary spilled return value before
     caller-save restoration.  Return live ranges therefore have to fit in
     the effective allocatable pool together; ordinary ranges may spill after
     them.  R0/R1 are deducted when packRegisters will reserve the indirect
     pointer pair.  Interval overlap reaches its maximum at one of the
     interval starts, so this O(n^2) check is exact for the live-range spans
           and identifies only the unrepresentable pressure shape. */
  for (symbol *start : call_results)
    {
      int demand = 0;
      for (symbol *candidate : call_results)
        if (candidate->liveFrom <= start->liveFrom &&
            candidate->liveTo >= start->liveFrom)
          demand += ralloc2_value_size (candidate);
      if (demand > call_result_capacity)
        return true;
    }

  if (pressure > 64)
    return true;
#endif

#ifdef MCS251_RALLOC2_DISABLE_LOW_DATA_SUPPORT
  if (options.model == MODEL_SMALL &&
      ((options.data_loc && options.data_loc < 0x30) ||
       (!options.data_loc && options.nostdinc)))
    return true;
#endif
  (void) ebbi;
  return false;
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

/* Port callback used by the production MCS-251 table and by the
   directed-test compiler (tests/check-ralloc2-directed.py). */
void
mcs251_ralloc2_assignRegisters (ebbIndex *ebbi)
{
  eBBlock **ebbs = ebbi->bbOrder;
  const int count = ebbi->count;
  iCode *ic;

  port->num_regs = MCS251_ALLOC_REG_COUNT;
  mcs251_ralloc2_sloc_num = 0;
  mcs251_ralloc2_spill_slots.clear ();
  mcs251_ralloc2_symbol_conflicts.clear ();
  mcs251_ptrRegReq = 0;

  if (ralloc2_test_mutation_rejects (ebbi))
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__,
              "ralloc2 test mutation disabled a supported path");
      exit (1);
    }

#ifdef MCS251_RALLOC2_TRACE_SELECTION
  /* Test-only route evidence.  The production compiler never defines this
     macro; directed gates use it to prove the ralloc2 callback is selected. */
  fprintf (stderr, "MCS251_RALLOC2_SELECTED:%s\n",
           currFunc && currFunc->name ? currFunc->name : "<anonymous>");
#endif

  mcs251_ralloc2_prepare (ebbi);
  prepare_live_ranges (ebbi);
  assign_bit_registers ();
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
          CHECK (((kinds[k] == kind_plain || kinds[k] == kind_float) &&
                 size >= 1 && size <= 8) ||
                 (kinds[k] == kind_pointer &&
                  size >= 1 && size <= kTargetPointerBytes),
                 "in_reg only for ABI-sized scalars and target pointers");
        if (size <= 0)
          CHECK (d == admit_fail, "non-positive sizes fail closed");
        else
          CHECK (d != admit_fail,
                 "expressible sizes never fail (spill instead)");
        if (size > 8)
          CHECK (d == admit_spill,
                 "wide values remain materialised spills");
      }

  /* Cost model. */
  CHECK (tuple_cost (6) < tuple_cost (4) && tuple_cost (4) < tuple_cost (2) &&
         tuple_cost (2) < tuple_cost (14) && tuple_cost (14) < tuple_cost (12) &&
         tuple_cost (12) < tuple_cost (8) && tuple_cost (8) < tuple_cost (0),
         "tuple preference mirrors legacy order");

  /* Assignment selection was introduced in MT-1C and remains enabled for
     the MT-1E Phase-2A production path. */
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
