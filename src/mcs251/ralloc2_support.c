/*-------------------------------------------------------------------------
  
  ralloc2_support.c - shared MCS-251 register/codegen preparation support

  This file contains only the register metadata, code-generation masks, and
  iCode packing transformations still used by the ralloc2 allocator.  The
  former serial allocator and its selection entry were removed in MT-1E
  Phase 2B.
-------------------------------------------------------------------------*/

#include "common.h"
#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

int mcs251_ptrRegReq;            /* one byte pointer register required */

/* 8051 registers */
reg_info mcs251_regs[] = {
  {REG_GPR, R7_IDX, REG_GPR, "r7", "ar7", "0", 7, 1},
  {REG_GPR, R6_IDX, REG_GPR, "r6", "ar6", "0", 6, 1},
  {REG_GPR, R5_IDX, REG_GPR, "r5", "ar5", "0", 5, 1},
  {REG_GPR, R4_IDX, REG_GPR, "r4", "ar4", "0", 4, 1},
  {REG_GPR, R3_IDX, REG_GPR, "r3", "ar3", "0", 3, 1},
  {REG_GPR, R2_IDX, REG_GPR, "r2", "ar2", "0", 2, 1},
  {REG_PTR, R1_IDX, REG_PTR, "r1", "ar1", "0", 1, 1},
  {REG_PTR, R0_IDX, REG_PTR, "r0", "ar0", "0", 0, 1},
  {REG_GPR, R15_IDX, REG_GPR, "r15", "r15", NULL, 15, 1},
  {REG_GPR, R14_IDX, REG_GPR, "r14", "r14", NULL, 14, 1},
  {REG_GPR, R13_IDX, REG_GPR, "r13", "r13", NULL, 13, 1},
  {REG_GPR, R12_IDX, REG_GPR, "r12", "r12", NULL, 12, 1},
  /* R11 aliases ACC and R10 aliases B; allocate them through A/B only. */
  {0, R11_IDX, 0, "r11", "r11", NULL, 11, 0},
  {0, R10_IDX, 0, "r10", "r10", NULL, 10, 0},
  {REG_GPR, R9_IDX, REG_GPR, "r9", "r9", NULL, 9, 1},
  {REG_GPR, R8_IDX, REG_GPR, "r8", "r8", NULL, 8, 1},
  {REG_BIT, B0_IDX, REG_BIT, "b0", "b0", "bits", 0, 1},
  {REG_BIT, B1_IDX, REG_BIT, "b1", "b1", "bits", 1, 1},
  {REG_BIT, B2_IDX, REG_BIT, "b2", "b2", "bits", 2, 1},
  {REG_BIT, B3_IDX, REG_BIT, "b3", "b3", "bits", 3, 1},
  {REG_BIT, B4_IDX, REG_BIT, "b4", "b4", "bits", 4, 1},
  {REG_BIT, B5_IDX, REG_BIT, "b5", "b5", "bits", 5, 1},
  {REG_BIT, B6_IDX, REG_BIT, "b6", "b6", "bits", 6, 1},
  {REG_BIT, B7_IDX, REG_BIT, "b7", "b7", "bits", 7, 1},
  {REG_CND, CND_IDX, REG_CND, "C", "not_psw", "0xd0", 0, 1},
  {0, DPL_IDX, 0, "dpl", "dpl", "0x82", 0, 0},
  {0, DPH_IDX, 0, "dph", "dph", "0x83", 0, 0},
  {0, B_IDX, 0, "b", "b", "0xf0", 0, 0},
  {0, A_IDX, 0, "a", "acc", "0xe0", 0, 0},
};

static const char* alt_regnames[] = {
  NULL, /* R7_IDX */
  NULL, /* R6_IDX */
  NULL, /* R5_IDX */
  NULL, /* R4_IDX */
  NULL, /* R3_IDX */
  NULL, /* R2_IDX */
  NULL, /* R1_IDX */
  NULL, /* R0_IDX */
  NULL, /* R15_IDX */
  NULL, /* R14_IDX */
  NULL, /* R13_IDX */
  NULL, /* R12_IDX */
  NULL, /* R11_IDX */
  NULL, /* R10_IDX */
  NULL, /* R9_IDX */
  NULL, /* R8_IDX */
  NULL, /* B0_IDX */
  NULL, /* B1_IDX */
  NULL, /* B2_IDX */
  NULL, /* B3_IDX */
  NULL, /* B4_IDX */
  NULL, /* B5_IDX */
  NULL, /* B6_IDX */
  NULL, /* B7_IDX */
  "c", /* CND_ID */
  NULL, /* DPL_IDX */
  NULL, /* DPH_IDX */
  NULL, /* B_IDX */
  NULL, /* A_IDX */
};

int mcs251_nRegs = MCS251_ALLOC_REG_COUNT;

int
mcs251_regname_to_idx (const char* reg_name)
{
  if (reg_name == NULL || *reg_name == '\0')
    return -1;

  char op[16];
  strncpy (op, reg_name, 15);
  op[15] = '\0';

  /* assuming that 'reg_name' could be a text snippet consisting of multiple
     insn operands, find the end of the first operand.  */
  char *op_end = &op[0];
  for (; op_end != &op[16]; ++op_end)
    if (*op_end == '\0' || *op_end == ',' || *op_end == ' ' || *op_end == '\t' || *op_end == ';')
      {
        *op_end = '\0';
        break;
      }

  for (int i = 0; i < END_IDX; ++i)
    {
       if (mcs251_regs[i].name && !strcmp (op, mcs251_regs[i].name))
         return i;

       if (alt_regnames[i] && !strcmp (op, alt_regnames[i]))
         return i;

       if (mcs251_regs[i].dname && !strcmp (op, mcs251_regs[i].dname))
        return i;
    }

  return -1;
}

/*-----------------------------------------------------------------*/
/* mcs251_regWithIdx - returns pointer to register with index number*/
/*-----------------------------------------------------------------*/
reg_info *
mcs251_regWithIdx (int idx)
{
  int i;

  for (i = 0; i < sizeof (mcs251_regs) / sizeof (reg_info); i++)
    if (mcs251_regs[i].rIdx == idx)
      return &mcs251_regs[i];

  werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "regWithIdx not found");
  exit (1);
}

/*-----------------------------------------------------------------*/
/* mcs251_regIdxForNumber - maps R0-R15 to tracked register index */
/*-----------------------------------------------------------------*/
int
mcs251_regIdxForNumber (unsigned int number)
{
  static const int indices[MCS251_BYTE_REG_COUNT] =
    {
      R0_IDX, R1_IDX, R2_IDX, R3_IDX,
      R4_IDX, R5_IDX, R6_IDX, R7_IDX,
      R8_IDX, R9_IDX, B_IDX, A_IDX,
      R12_IDX, R13_IDX, R14_IDX, R15_IDX
    };

  wassert (number < MCS251_BYTE_REG_COUNT);
  return indices[number];
}


static bitVect *mcs251_all_bitregs;
static bitVect *mcs251_all_bankregs;
static int mcs251_masks_nregs = -1;

static void
mcs251_ensure_register_masks (void)
{
  int j;

  if (mcs251_masks_nregs == mcs251_nRegs &&
      mcs251_all_bitregs && mcs251_all_bankregs)
    return;

  if (mcs251_all_bitregs)
    freeBitVect (mcs251_all_bitregs);
  if (mcs251_all_bankregs)
    freeBitVect (mcs251_all_bankregs);

  mcs251_all_bitregs = newBitVect (mcs251_nRegs);
  mcs251_all_bankregs = newBitVect (mcs251_nRegs);
  for (j = 0; j < mcs251_nRegs; ++j)
    {
      if (mcs251_regs[j].type == REG_BIT)
        mcs251_all_bitregs =
          bitVectSetBit (mcs251_all_bitregs, mcs251_regs[j].rIdx);
      if (mcs251_regs[j].type == REG_GPR ||
          mcs251_regs[j].type == REG_PTR)
        mcs251_all_bankregs =
          bitVectSetBit (mcs251_all_bankregs, mcs251_regs[j].rIdx);
    }
  mcs251_masks_nregs = mcs251_nRegs;
}

bitVect *
mcs251_allBitregs (void)
{
  mcs251_ensure_register_masks ();
  return mcs251_all_bitregs;
}

bitVect *
mcs251_allBankregs (void)
{
  mcs251_ensure_register_masks ();
  return mcs251_all_bankregs;
}

/* rUmaskForOp :- returns register mask for an operand             */
/*-----------------------------------------------------------------*/
bitVect *
mcs251_rUmaskForOp (operand * op)
{
  bitVect *rumask;
  symbol *sym;
  int j;

  /* only temporaries are assigned registers */
  if (!IS_ITEMP (op))
    return NULL;

  sym = OP_SYMBOL (op);

  /* if spilt or no registers assigned to it
     then nothing */
  if (sym->isspilt || !sym->nRegs)
    return NULL;

  rumask = newBitVect (mcs251_nRegs);

  for (j = 0; j < sym->nRegs; j++)
    {
      if (sym->regs[j])         /* EEP - debug */
        rumask = bitVectSetBit (rumask, sym->regs[j]->rIdx);
    }

  return rumask;
}

/* farSpacePackable - returns the packable icode for far variables */
/*-----------------------------------------------------------------*/
static iCode *
farSpacePackable (iCode * ic)
{
  iCode *dic;

  /* go thru till we find a definition for the
     symbol on the right */
  for (dic = ic->prev; dic; dic = dic->prev)
    {
      /* if the definition is a call then no */
      if ((dic->op == CALL || dic->op == PCALL) && IC_RESULT (dic)->key == IC_RIGHT (ic)->key)
        {
          return NULL;
        }

      /* if shift by unknown amount then not */
      if ((dic->op == LEFT_OP || dic->op == RIGHT_OP) && IC_RESULT (dic)->key == IC_RIGHT (ic)->key)
        return NULL;

      /* if pointer get and size > 1 */
      if (POINTER_GET (dic) && getSize (aggrToPtr (operandType (IC_LEFT (dic)), FALSE)) > 1)
        return NULL;

      if (POINTER_SET (dic) && getSize (aggrToPtr (operandType (IC_RESULT (dic)), FALSE)) > 1)
        return NULL;

      if (dic->op == IFX)
        {
          if (IC_COND (dic) && IS_TRUE_SYMOP (IC_COND (dic)) && isOperandInFarSpace (IC_COND (dic)))
            return NULL;
        }
      else if (dic->op == JUMPTABLE)
        {
          if (IC_JTCOND (dic) && IS_TRUE_SYMOP (IC_JTCOND (dic)) && isOperandInFarSpace (IC_JTCOND (dic)))
            return NULL;
        }
      else
        {
          /* if any tree is a true symbol in far space */
          if (IC_RESULT (dic) && IS_TRUE_SYMOP (IC_RESULT (dic)) && isOperandInFarSpace (IC_RESULT (dic)))
            return NULL;

          if (IC_RIGHT (dic) &&
              IS_TRUE_SYMOP (IC_RIGHT (dic)) &&
              isOperandInFarSpace (IC_RIGHT (dic)) && !isOperandEqual (IC_RIGHT (dic), IC_RESULT (ic)))
            return NULL;

          if (IC_LEFT (dic) &&
              IS_TRUE_SYMOP (IC_LEFT (dic)) &&
              isOperandInFarSpace (IC_LEFT (dic)) && !isOperandEqual (IC_LEFT (dic), IC_RESULT (ic)))
            return NULL;
        }

      if (isOperandEqual (IC_RIGHT (ic), IC_RESULT (dic)))
        {
          if ((dic->op == LEFT_OP || dic->op == RIGHT_OP || dic->op == '-') && IS_OP_LITERAL (IC_RIGHT (dic)))
            return NULL;
          else
            return dic;
        }
    }

  return NULL;
}

/*-----------------------------------------------------------------*/
/* packRegsForAssign - register reduction for assignment           */
/*-----------------------------------------------------------------*/
static int
packRegsForAssign (iCode * ic, eBBlock * ebp)
{
  iCode *dic, *sic;

  if (!IS_ITEMP (IC_RIGHT (ic)) || OP_SYMBOL (IC_RIGHT (ic))->isind || OP_LIVETO (IC_RIGHT (ic)) > ic->seq)
    {
      return 0;
    }

  /* if the true symbol is defined in far space or on stack
     then we should not since this will increase register pressure */
  if (isOperandInFarSpace (IC_RESULT (ic)) && !farSpacePackable (ic))
    {
      return 0;
    }

  /* find the definition of iTempNN scanning backwards if we find
     a use of the true symbol before we find the definition then
     we cannot */
  for (dic = ic->prev; dic; dic = dic->prev)
    {
      int crossedCall = 0;

      /* We can pack across a function call only if it's a local */
      /* variable or our parameter. Never pack global variables */
      /* or parameters to a function we call. */
      if ((dic->op == CALL || dic->op == PCALL))
        {
          if (!OP_SYMBOL (IC_RESULT (ic))->ismyparm && !OP_SYMBOL (IC_RESULT (ic))->islocal)
            {
              crossedCall = 1;
            }
        }

      if (dic->op == INLINEASM)
        {
          dic = NULL;
          break;
        }

      /* Don't move an assignment out of a critical block */
      if (dic->op == CRITICAL)
        {
          dic = NULL;
          break;
        }

      if (SKIP_IC2 (dic))
        continue;

      if (dic->op == IFX)
        {
          if (IS_SYMOP (IC_COND (dic)) &&
              (IC_COND (dic)->key == IC_RESULT (ic)->key || IC_COND (dic)->key == IC_RIGHT (ic)->key))
            {
              dic = NULL;
              break;
            }
        }
      else
        {
          if (IS_TRUE_SYMOP (IC_RESULT (dic)) && IS_OP_VOLATILE (IC_RESULT (dic)))
            {
              dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_RESULT (dic)) && IC_RESULT (dic)->key == IC_RIGHT (ic)->key)
            {
              if (POINTER_SET (dic))
                dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_RIGHT (dic)) &&
              (IC_RIGHT (dic)->key == IC_RESULT (ic)->key || IC_RIGHT (dic)->key == IC_RIGHT (ic)->key))
            {
              dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_LEFT (dic)) &&
              (IC_LEFT (dic)->key == IC_RESULT (ic)->key || IC_LEFT (dic)->key == IC_RIGHT (ic)->key))
            {
              dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_RESULT (dic)) && IC_RESULT (dic)->key == IC_RESULT (ic)->key)
            {
              dic = NULL;
              break;
            }

          if (crossedCall)
            {
              dic = NULL;
              break;
            }
        }
    }

  if (!dic)
    return 0;                   /* did not find */

  /* if assignment then check that right is not a bit */
  if (ASSIGNMENT (ic) && !POINTER_SET (ic))
    {
      sym_link *etype = operandType (IC_RESULT (dic));
      if (IS_BITFIELD (etype))
        {
          /* if result is a bit too then it's ok */
          etype = operandType (IC_RESULT (ic));
          if (!IS_BITFIELD (etype))
            {
              return 0;
            }
        }
    }

  /* if the result is on stack or iaccess then it must be
     the same atleast one of the operands */
  if (OP_SYMBOL (IC_RESULT (ic))->onStack || OP_SYMBOL (IC_RESULT (ic))->iaccess)
    {
      /* the operation has only one symbol
         operator then we can pack */
      if ((IC_LEFT (dic) && !IS_SYMOP (IC_LEFT (dic))) || (IC_RIGHT (dic) && !IS_SYMOP (IC_RIGHT (dic))))
        goto pack;

      if (!((IC_LEFT (dic) &&
             IC_RESULT (ic)->key == IC_LEFT (dic)->key) || (IC_RIGHT (dic) && IC_RESULT (ic)->key == IC_RIGHT (dic)->key)))
        return 0;
    }
pack:
  /* found the definition */

  /* delete from liverange table also
     delete from all the points inbetween and the new
     one */
  for (sic = dic; sic != ic; sic = sic->next)
    {
      bitVectUnSetBit (sic->rlive, IC_RESULT (ic)->key);
      if (IS_ITEMP (IC_RESULT (dic)))
        bitVectSetBit (sic->rlive, IC_RESULT (dic)->key);
    }

  /* replace the result with the result of */
  /* this assignment and remove this assignment */
  bitVectUnSetBit (OP_SYMBOL (IC_RESULT (dic))->defs, dic->key);
  ReplaceOpWithCheaperOp (&IC_RESULT (dic), IC_RESULT (ic));

  if (IS_ITEMP (IC_RESULT (dic)) && OP_SYMBOL (IC_RESULT (dic))->liveFrom > dic->seq)
    {
      OP_SYMBOL (IC_RESULT (dic))->liveFrom = dic->seq;
    }
  // TODO: and the otherway around?

  remiCodeFromeBBlock (ebp, ic);
  bitVectUnSetBit (OP_DEFS (IC_RESULT (ic)), ic->key);
  hTabDeleteItem (&iCodehTab, ic->key, ic, DELETE_ITEM, NULL);
  OP_DEFS (IC_RESULT (dic)) = bitVectSetBit (OP_DEFS (IC_RESULT (dic)), dic->key);
  return 1;
}

/*------------------------------------------------------------------*/
/* findAssignToSym : scanning backwards looks for first assig found */
/*------------------------------------------------------------------*/
static iCode *
findAssignToSym (operand * op, iCode * ic)
{
  iCode *dic;

  /* This routine is used to find sequences like
     iTempAA = FOO;
     ...;  (intervening ops don't use iTempAA or modify FOO)
     blah = blah + iTempAA;

     and eliminate the use of iTempAA, freeing up its register for
     other uses.
   */

  for (dic = ic->prev; dic; dic = dic->prev)
    {
      /* if definition by assignment */
      if (dic->op == '=' && !POINTER_SET (dic) && IC_RESULT (dic)->key == op->key
/*          &&  IS_TRUE_SYMOP(IC_RIGHT(dic)) */
        )
        break;                  /* found where this temp was defined */

      /* if we find an usage then we cannot delete it */
      if (IC_LEFT (dic) && IC_LEFT (dic)->key == op->key)
        return NULL;

      if (IC_RIGHT (dic) && IC_RIGHT (dic)->key == op->key)
        return NULL;

      if (POINTER_SET (dic) && IC_RESULT (dic)->key == op->key)
        return NULL;
    }

  if (!dic)
    return NULL;                /* didn't find any assignment to op */

  /* we are interested only if defined in far space */
  /* or in stack space in case of + & - */

  /* if assigned to a non-symbol then don't repack regs */
  if (!IS_SYMOP (IC_RIGHT (dic)))
    return NULL;

  /* if the symbol is volatile then we should not */
  if (isOperandVolatile (IC_RIGHT (dic), TRUE))
    return NULL;
  /* XXX TODO --- should we be passing FALSE to isOperandVolatile()?
     What does it mean for an iTemp to be volatile, anyway? Passing
     TRUE is more cautious but may prevent possible optimizations */

  /* if the symbol is in far space then we should not */
  if (isOperandInFarSpace (IC_RIGHT (dic)))
    return NULL;

  /* for + & - operations make sure that
     if it is on the stack it is the same
     as one of the three operands */
  if ((ic->op == '+' || ic->op == '-') && OP_SYMBOL (IC_RIGHT (dic))->onStack)
    {
      if (IC_RESULT (ic)->key != IC_RIGHT (dic)->key &&
          IC_LEFT (ic)->key != IC_RIGHT (dic)->key && IC_RIGHT (ic)->key != IC_RIGHT (dic)->key)
        return NULL;
    }

  /* now make sure that the right side of dic
     is not defined between ic & dic */
  if (dic)
    {
      iCode *sic = dic->next;

      for (; sic != ic; sic = sic->next)
        if (IC_RESULT (sic) && IC_RESULT (sic)->key == IC_RIGHT (dic)->key)
          return NULL;
    }

  return dic;
}

/*-----------------------------------------------------------------*/
/* reassignAliasedSym - used by packRegsForSupport to replace      */
/*                      redundant iTemp with equivalent symbol     */
/*-----------------------------------------------------------------*/
static void
reassignAliasedSym (eBBlock * ebp, iCode * assignment, iCode * use, operand * op)
{
  iCode *ic;
  unsigned oldSymKey, newSymKey;

  oldSymKey = op->key;
  newSymKey = IC_RIGHT (assignment)->key;

  /* only track live ranges of compiler-generated temporaries */
  if (!IS_ITEMP (IC_RIGHT (assignment)))
    newSymKey = 0;

  /* update the live-value bitmaps */
  for (ic = assignment; ic != use; ic = ic->next)
    {
      bitVectUnSetBit (ic->rlive, oldSymKey);
      if (newSymKey != 0)
        ic->rlive = bitVectSetBit (ic->rlive, newSymKey);
    }

  /* update the sym of the used operand */
  OP_SYMBOL (op) = OP_SYMBOL (IC_RIGHT (assignment));
  op->key = OP_SYMBOL (op)->key;
  OP_SYMBOL (op)->accuse = 0;

  /* update the sym's liverange */
  if (OP_LIVETO (op) < ic->seq)
    setToRange (op, ic->seq, FALSE);

  /* remove the assignment iCode now that its result is unused */
  remiCodeFromeBBlock (ebp, assignment);
  bitVectUnSetBit (OP_SYMBOL (IC_RESULT (assignment))->defs, assignment->key);
  hTabDeleteItem (&iCodehTab, assignment->key, assignment, DELETE_ITEM, NULL);
}


/*-----------------------------------------------------------------*/
/* packRegsForSupport :- reduce some registers for support calls   */
/*-----------------------------------------------------------------*/
static int
packRegsForSupport (iCode * ic, eBBlock * ebp)
{
  iCode *dic;

  /* for the left & right operand :- look to see if the
     left was assigned a true symbol in far space in that
     case replace them */

  if (IS_ITEMP (IC_LEFT (ic)) && OP_SYMBOL (IC_LEFT (ic))->liveTo <= ic->seq)
    {
      dic = findAssignToSym (IC_LEFT (ic), ic);

      if (dic)
        {
          /* found it we need to remove it from the block */
          reassignAliasedSym (ebp, dic, ic, IC_LEFT (ic));
          return 1;
        }
    }

  /* do the same for the right operand */
  if (IS_ITEMP (IC_RIGHT (ic)) && OP_SYMBOL (IC_RIGHT (ic))->liveTo <= ic->seq)
    {
      iCode *dic = findAssignToSym (IC_RIGHT (ic), ic);

      if (dic)
        {
          /* if this is a subtraction & the result
             is a true symbol in far space then don't pack */
          if (ic->op == '-' && IS_TRUE_SYMOP (IC_RESULT (dic)))
            {
              sym_link *etype = getSpec (operandType (IC_RESULT (dic)));
              if (IN_FARSPACE (SPEC_OCLS (etype)))
                return 0;
            }
          /* found it we need to remove it from the block */
          reassignAliasedSym (ebp, dic, ic, IC_RIGHT (ic));

          return 1;
        }
    }

  return 0;
}


/*-----------------------------------------------------------------*/
/* packRegsForOneuse : - will reduce some registers for single Use */
/*-----------------------------------------------------------------*/
static iCode *
packRegsForOneuse (iCode * ic, operand * op, eBBlock * ebp)
{
  iCode *dic, *sic;
  sym_link *type;
  int usingCarry=0;

  /* if returning a literal then do nothing */
  if (!IS_ITEMP (op))
    return NULL;

  /* if rematerializable or already return use then do nothing */
  if (OP_SYMBOL (op)->remat || OP_SYMBOL (op)->ruonly)
    return NULL;

  /* only upto 2 bytes since we cannot predict
     the usage of b, & acc */
  type = operandType (op);
  if (getSize (type) > (mcs251_fReturnSize - 2))
    return NULL;
  usingCarry = IS_BIT(type);

  if (ic->op != RETURN && ic->op != SEND && !POINTER_SET (ic) && !POINTER_GET (ic))
    return NULL;

  if (ic->op == SEND && ic->argreg != 1)
    return NULL;

  /* this routine will mark the symbol as used in one
     instruction use only && if the definition is local
     (ie. within the basic block) && has only one definition &&
     that definition is either a return value from a
     function or does not contain any variables in
     far space */
  if (bitVectnBitsOn (OP_USES (op)) > 1)
    return NULL;

  /* if it has only one definition */
  if (bitVectnBitsOn (OP_DEFS (op)) > 1)
    return NULL;                /* has more than one definition */

  /* get that definition */
  if (!(dic = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (op)))))
    return NULL;

  /* if that only usage is a cast */
  if (dic->op == CAST)
    {
      /* to a bigger type */
      if (getSize (OP_SYM_TYPE (IC_RESULT (dic))) > getSize (OP_SYM_TYPE (IC_RIGHT (dic))))
        {
          /* then we can not, since we cannot predict the usage of b & acc */
          return NULL;
        }
    }

  /* found the definition now check if it is local */
  if (dic->seq < ebp->fSeq || dic->seq > ebp->lSeq)
    return NULL;                /* non-local */

  /* now check if it is the return from a function call */
  if (dic->op == CALL || dic->op == PCALL)
    {
      if (ic->op != SEND && ic->op != RETURN && !POINTER_SET (ic) && !POINTER_GET (ic))
        {
          OP_SYMBOL (op)->ruonly = 1;
          return dic;
        }
    }
  else
    {
      /* otherwise check that the definition does
         not contain any symbols in far space */
      if (isOperandInFarSpace (IC_LEFT (dic)) ||
          isOperandInFarSpace (IC_RIGHT (dic)) || IS_OP_RUONLY (IC_LEFT (ic)) || IS_OP_RUONLY (IC_RIGHT (ic)))
        {
          return NULL;
        }

      /* if pointer set then make sure the pointer is one byte */
      if (POINTER_SET (dic) && !IS_SMALL_PTR (aggrToPtr (operandType (IC_RESULT (dic)), FALSE)))
        return NULL;

      if (POINTER_GET (dic) && !IS_SMALL_PTR (aggrToPtr (operandType (IC_LEFT (dic)), FALSE)))
        return NULL;
    }

  /* Make sure no overlapping liverange is already assigned to DPTR */
  if (OP_SYMBOL (op)->clashes)
    {
      symbol *sym;
      int i;

      for (i = 0; i < OP_SYMBOL (op)->clashes->size; i++)
        {
          if (bitVectBitValue (OP_SYMBOL (op)->clashes, i))
            {
              sym = hTabItemWithKey (liveRanges, i);
              if (sym->ruonly)
                return NULL;
            }
        }
    }

  sic = dic;

  if (ic->op == SEND)
    {
      /* look for the call to extend following
         far space search to include all parameters.
         see bug 3004918 */
      for (; ic; ic = ic->next)
        if (ic->op == CALL || ic->op == PCALL)
          break;
      if (!ic)                  /* not found */
        return NULL;
    }

  if (ic->op == PCALL && !IS_SMALL_PTR(aggrToPtr(operandType(IC_LEFT(ic)), FALSE)))
    return NULL;

  /* make sure the intervening instructions
     don't have anything in far space */
  for (dic = dic->next; dic && dic != ic && sic != ic; dic = dic->next)
    {
      /* if there is an intervening function call then no */
      if (dic->op == CALL || dic->op == PCALL)
        return NULL;
      /* if pointer set then make sure the pointer
         is one byte */
      if (POINTER_SET (dic) && !IS_SMALL_PTR (aggrToPtr (operandType (IC_RESULT (dic)), FALSE)))
        return NULL;

      if (POINTER_GET (dic) && !IS_SMALL_PTR (aggrToPtr (operandType (IC_LEFT (dic)), FALSE)))
        return NULL;

      /* if address of & the result is remat then okay */
      if (dic->op == ADDRESS_OF && OP_SYMBOL (IC_RESULT (dic))->remat)
        continue;

      /* if operand has size of three or more & this
         operation is a '*','/' or '%' then 'b' may
         cause a problem */
      if ((dic->op == '%' || dic->op == '/' || dic->op == '*') && getSize (operandType (op)) >= 3)
        return NULL;

      /* if left or right or result is in far space */
      if (isOperandInFarSpace (IC_LEFT (dic)) ||
          isOperandInFarSpace (IC_RIGHT (dic)) ||
          isOperandInFarSpace (IC_RESULT (dic)) ||
          IS_OP_RUONLY (IC_LEFT (dic)) || IS_OP_RUONLY (IC_RIGHT (dic)) || IS_OP_RUONLY (IC_RESULT (dic)))
        {
          return NULL;
        }
      /* if left or right or result is on stack */
      if (isOperandOnStack (IC_LEFT (dic)) || isOperandOnStack (IC_RIGHT (dic)) || isOperandOnStack (IC_RESULT (dic)))
        {
          return NULL;
        }
      if (usingCarry)
        {
          if (isOperandInBitSpace (IC_LEFT (dic)) ||
              isOperandInBitSpace (IC_RIGHT (dic)) ||
              isOperandInBitSpace (IC_RESULT (dic)))
            {
              return NULL;
            }
          if (dic->op != SEND || dic->op != IPUSH || dic->op != '=')
            {
              return NULL;
            }
        }
    }

  OP_SYMBOL (op)->ruonly = 1;
  return sic;
}

/*-----------------------------------------------------------------*/
/* isBitwiseOptimizable - requirements of JEAN LOUIS VERN          */
/*-----------------------------------------------------------------*/
static bool
isBitwiseOptimizable (iCode * ic)
{
  sym_link *ltype = getSpec (operandType (IC_LEFT (ic)));
  sym_link *rtype = getSpec (operandType (IC_RIGHT (ic)));

  /* bitwise operations are considered optimizable
     under the following conditions (Jean-Louis VERN)

     x & lit
     bit & bit
     bit & x
     bit ^ bit
     bit ^ x
     x   ^ lit
     x   | lit
     bit | bit
     bit | x
   */
  if (IS_LITERAL (rtype) || (IS_BITVAR (ltype) && IN_BITSPACE (SPEC_OCLS (ltype))))
    return TRUE;
  else
    return FALSE;
}

/*-----------------------------------------------------------------*/
/* mcs251_isCommutativeOp - tests whether this op cares what order its    */
/*                   operands are in                               */
/*-----------------------------------------------------------------*/
bool
mcs251_isCommutativeOp (unsigned int op)
{
  if (op == '+' || op == '*' || op == EQ_OP || op == '^' || op == '|' || op == BITWISEAND)
    return TRUE;
  else
    return FALSE;
}

/*-----------------------------------------------------------------*/
/* mcs251_operandUsesAcc - determines whether the code generated for this */
/*                  operand will have to use the accumulator       */
/*-----------------------------------------------------------------*/
bool
mcs251_operandUsesAcc (operand * op, bool allowBitspace)
{
  if (!op)
    return FALSE;

  if (IS_SYMOP (op))
    {
      symbol *sym = OP_SYMBOL (op);
      memmap *symspace;

      if (sym->accuse)
        return TRUE;            /* duh! */

      if (IN_STACK (sym->etype) || sym->onStack || (SPIL_LOC (op) && SPIL_LOC (op)->onStack))
        return TRUE;            /* acc is used to calc stack offset */

      if (IS_ITEMP (op))
        {
          if (SPIL_LOC (op))
            {
              sym = SPIL_LOC (op);      /* if spilled, look at spill location */
            }
          else
            {
              return FALSE;     /* more checks? */
            }
        }

      symspace = SPEC_OCLS (sym->etype);

      if (sym->iaccess && symspace->paged)
        return TRUE;            /* must fetch paged indirect sym via accumulator */

      if (!allowBitspace && IN_BITSPACE (symspace))
        return TRUE;            /* fetching bit vars uses the accumulator */

      if (IN_FARSPACE (symspace) || IN_CODESPACE (symspace))
        return TRUE;            /* fetched via accumulator and dptr */
    }

  return FALSE;
}

/*-----------------------------------------------------------------*/
/* packRegsForAccUse - pack registers for acc use                  */
/*-----------------------------------------------------------------*/
static void
packRegsForAccUse (iCode * ic)
{
  iCode *uic;

  /* if this is an aggregate, e.g. a one byte char array */
  if (IS_AGGREGATE (operandType (IC_RESULT (ic))))
    return;

  /* if we are calling a reentrant function that has stack parameters */
  if (ic->op == CALL && IFFUNC_ISREENT (operandType (IC_LEFT (ic))) && FUNC_HASSTACKPARM (operandType (IC_LEFT (ic))))
    return;

  if (ic->op == PCALL &&
      IFFUNC_ISREENT (operandType (IC_LEFT (ic))->next) && FUNC_HASSTACKPARM (operandType (IC_LEFT (ic))->next))
    return;

  /* if + or - then it has to be one byte result */
  if ((ic->op == '+' || ic->op == '-') && getSize (operandType (IC_RESULT (ic))) > 1)
    return;

  /* if shift operation make sure right side is not a literal */
  if (ic->op == RIGHT_OP && (isOperandLiteral (IC_RIGHT (ic)) || getSize (operandType (IC_RESULT (ic))) > 1))
    return;

  if (ic->op == LEFT_OP && (isOperandLiteral (IC_RIGHT (ic)) || getSize (operandType (IC_RESULT (ic))) > 1))
    return;

  if (IS_BITWISE_OP (ic) && getSize (operandType (IC_RESULT (ic))) > 1)
    return;

  /* has only one definition */
  if (bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) > 1)
    return;

  /* has only one use */
  if (bitVectnBitsOn (OP_USES (IC_RESULT (ic))) > 1)
    return;

  /* and the usage immediately follows this iCode */
  if (!(uic = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_USES (IC_RESULT (ic))))))
    return;

  if (ic->next != uic)
    return;

  /* if it is a conditional branch then we definitely can */
  if (uic->op == IFX)
    goto accuse;

  if (uic->op == JUMPTABLE)
    return;

  if (POINTER_SET (uic) && getSize (aggrToPtr (operandType (IC_RESULT (uic)), FALSE)) > 1)
    return;

  /* if the usage is not an assignment
     or an arithmetic / bitwise / shift operation then not */
  if (uic->op != '=' && !IS_ARITHMETIC_OP (uic) && !IS_BITWISE_OP (uic) && uic->op != LEFT_OP && uic->op != RIGHT_OP)
    return;

  /* if shift operation make sure right side is not a literal */
  /* WIML: Why is this? */
  if (uic->op == RIGHT_OP && (isOperandLiteral (IC_RIGHT (uic)) || getSize (operandType (IC_RESULT (uic))) > 1))
    return;
  if (uic->op == LEFT_OP && (isOperandLiteral (IC_RIGHT (uic)) || getSize (operandType (IC_RESULT (uic))) > 1))
    return;

  /* make sure that the result of this icode is not on the
     stack, since acc is used to compute stack offset */
#if 0
  if (IS_TRUE_SYMOP (IC_RESULT (uic)) && OP_SYMBOL (IC_RESULT (uic))->onStack)
    return;
#else
  if (isOperandOnStack (IC_RESULT (uic)))
    return;
#endif

  /* if the usage has only one operand then we can */
  if (IC_LEFT (uic) == NULL || IC_RIGHT (uic) == NULL)
    goto accuse;

  /* if the other operand uses the accumulator then we cannot */
  if ((IC_LEFT (uic)->key == IC_RESULT (ic)->key &&
       mcs251_operandUsesAcc (IC_RIGHT (uic), IS_BIT (operandType (IC_LEFT (uic))))) ||
      (IC_RIGHT (uic)->key == IC_RESULT (ic)->key && mcs251_operandUsesAcc (IC_LEFT (uic), IS_BIT (operandType (IC_RIGHT (uic))))))
    return;

  /* make sure this is on the left side if not commutative */
  /* except for '-', which has been written to be able to
     handle reversed operands */
  if (!(mcs251_isCommutativeOp (ic->op) || ic->op == '-') && IC_LEFT (uic)->key != IC_RESULT (ic)->key)
    return;

  /* Sign handling will overwrite a */
  if (uic->op == '*' && getSize (operandType (IC_RESULT (uic))) > 1 &&
    (!SPEC_USIGN (getSpec (operandType (IC_LEFT (uic)))) || !SPEC_USIGN (getSpec (operandType (IC_RIGHT (uic))))))
    return;
  if ((uic->op == '/' || uic->op == '%') &&
    (!SPEC_USIGN (getSpec (operandType (IC_LEFT (uic)))) || !SPEC_USIGN (getSpec (operandType (IC_RIGHT (uic))))))
    return;

#if 0
  // this is too dangerous and need further restrictions
  // see bug #447547

  /* if one of them is a literal then we can */
  if ((IC_LEFT (uic) && IS_OP_LITERAL (IC_LEFT (uic))) || (IC_RIGHT (uic) && IS_OP_LITERAL (IC_RIGHT (uic))))
    {
      OP_SYMBOL (IC_RESULT (ic))->accuse = 1;
      return;
    }
#endif

accuse:
  OP_SYMBOL (IC_RESULT (ic))->accuse = 1;
}

/*-----------------------------------------------------------------*/
/* packForPush - heuristics to reduce iCode for pushing            */
/*-----------------------------------------------------------------*/
static void
packForPush (iCode * ic, eBBlock ** ebpp, int blockno)
{
  iCode *dic, *lic;
  bitVect *dbv;
  struct eBBlock *ebp = ebpp[blockno];
  int disallowHiddenAssignment = 0;

  if (ic->op != IPUSH || !IS_ITEMP (IC_LEFT (ic)))
    return;

  /* must have only definition & one usage */
  if (bitVectnBitsOn (OP_DEFS (IC_LEFT (ic))) != 1 || bitVectnBitsOn (OP_USES (IC_LEFT (ic))) != 1)
    return;

  /* The changes in SDCCopt.c #7741 should correct the use info, making */
  /* this extra test redundant. */
  if (ic->parmPush)
    {// find Send or other Push for this func call
      for (lic = ic->next; lic && lic->op != CALL; lic = lic->next)
        {
          if ((lic->op == IPUSH || lic->op == SEND) && IS_ITEMP (IC_LEFT (lic)))
            {// and check parameter is not passed again
              symbol * parm = OP_SYMBOL (IC_LEFT (ic));
              symbol * other = OP_SYMBOL (IC_LEFT (lic));
              if (other == parm)
                return;
            }
        }
    }

  /* find the definition */
  if (!(dic = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (IC_LEFT (ic))))))
    return;

  if (dic->op != '=' || POINTER_SET (dic))
    return;

  if (dic->seq < ebp->fSeq)     // Evelyn did this
    {
      int i;
      for (i = 0; i < blockno; i++)
        {
          if (dic->seq >= ebpp[i]->fSeq && dic->seq <= ebpp[i]->lSeq)
            {
              ebp = ebpp[i];
              break;
            }
        }
      wassert (i != blockno);   // no way to recover from here
    }

  if (IS_SYMOP (IC_RIGHT (dic)))
    {
      if (IC_RIGHT (dic)->isvolatile)
        return;

      if (OP_SYMBOL (IC_RIGHT (dic))->addrtaken || isOperandGlobal (IC_RIGHT (dic)))
        disallowHiddenAssignment = 1;

      /* make sure the right side does not have any definitions
         inbetween */
      dbv = OP_DEFS (IC_RIGHT (dic));
      for (lic = ic; lic && lic != dic; lic = lic->prev)
        {
          if (bitVectBitValue (dbv, lic->key))
            return;
          if (disallowHiddenAssignment && (lic->op == CALL || lic->op == PCALL || POINTER_SET (lic)))
            return;
        }
      /* make sure they have the same type */
      if (IS_SPEC (operandType (IC_LEFT (ic))))
        {
          sym_link *itype = operandType (IC_LEFT (ic));
          sym_link *ditype = operandType (IC_RIGHT (dic));

          if (SPEC_USIGN (itype) != SPEC_USIGN (ditype) || SPEC_LONG (itype) != SPEC_LONG (ditype))
            return;
        }
      /* extend the live range of replaced operand if needed */
      if (OP_SYMBOL (IC_RIGHT (dic))->liveTo < ic->seq)
        {
          OP_SYMBOL (IC_RIGHT (dic))->liveTo = ic->seq;
        }
      bitVectUnSetBit (OP_SYMBOL (IC_RESULT (dic))->defs, dic->key);
    }
  if (IS_ITEMP (IC_RIGHT (dic)))
    OP_USES (IC_RIGHT (dic)) = bitVectSetBit (OP_USES (IC_RIGHT (dic)), ic->key);

  /* now we know that it has one & only one def & use
     and the that the definition is an assignment */
  ReplaceOpWithCheaperOp (&IC_LEFT (ic), IC_RIGHT (dic));
  remiCodeFromeBBlock (ebp, dic);
  hTabDeleteItem (&iCodehTab, dic->key, dic, DELETE_ITEM, NULL);
}

/*-----------------------------------------------------------------*/
/* packRegisters - does some transformations to reduce register    */
/*                   pressure                                      */
/*-----------------------------------------------------------------*/
static void
packRegisters (eBBlock ** ebpp, int blockno)
{
  iCode *ic;
  int change = 0;
  eBBlock *ebp = ebpp[blockno];

  do
    {
      change = 0;

      /* look for assignments of the form */
      /* iTempNN = TrueSym (someoperation) SomeOperand */
      /*       ....                       */
      /* TrueSym := iTempNN:1             */
      for (ic = ebp->sch; ic; ic = ic->next)
        {
          /* find assignment of the form TrueSym := iTempNN:1 */
          if (ic->op == '=' && !POINTER_SET (ic))
            change += packRegsForAssign (ic, ebp);
        }
    }
  while (change);

  for (ic = ebp->sch; ic; ic = ic->next)
    {
      /* Fix for bug #979599:   */
      /* P0 &= ~1;              */

      /* Look for two subsequent iCodes with */
      /*   iTemp := _c;         */
      /*   _c = iTemp & op;     */
      /* and replace them by    */
      /*   iTemp := _c;         */
      /*   _c = _c & op;        */
      if ((ic->op == BITWISEAND || ic->op == '|' || ic->op == '^') &&
          ic->prev &&
          ic->prev->op == '=' &&
          (IS_ITEMP (IC_LEFT (ic)) && isOperandEqual (IC_LEFT (ic), IC_RESULT (ic->prev)) && isOperandEqual (IC_RESULT (ic), IC_RIGHT (ic->prev)) ||
           IS_ITEMP (IC_RIGHT (ic)) && isOperandEqual (IC_RIGHT (ic), IC_RESULT (ic->prev)) && isOperandEqual (IC_RESULT (ic), IC_RIGHT (ic->prev))))
        {
          bool left = IS_ITEMP (IC_LEFT (ic)) && isOperandEqual (IC_LEFT (ic), IC_RESULT (ic->prev)) && isOperandEqual (IC_RESULT (ic), IC_RIGHT (ic->prev));

          iCode *ic_prev = ic->prev;
          symbol *prev_result_sym = OP_SYMBOL (IC_RESULT (ic_prev));

          ReplaceOpWithCheaperOp (left ? &IC_LEFT (ic) : &IC_RIGHT (ic), IC_RESULT (ic));
          if (IC_RESULT (ic_prev) != (left ? IC_RIGHT (ic) : IC_LEFT (ic)))
            {
              bitVectUnSetBit (OP_USES (IC_RESULT (ic_prev)), ic->key);
              if (              /*IS_ITEMP (IC_RESULT (ic_prev)) && */
                   prev_result_sym->liveTo == ic->seq)
                {
                  prev_result_sym->liveTo = ic_prev->seq;
                }
            }
          bitVectSetBit (OP_USES (IC_RESULT (ic)), ic->key);

          bitVectSetBit (ic->rlive, IC_RESULT (ic)->key);

          if (bitVectIsZero (OP_USES (IC_RESULT (ic_prev))))
            {
              bitVectUnSetBit (ic->rlive, IC_RESULT (ic)->key);
              bitVectUnSetBit (OP_DEFS (IC_RESULT (ic_prev)), ic_prev->key);
              remiCodeFromeBBlock (ebp, ic_prev);
              hTabDeleteItem (&iCodehTab, ic_prev->key, ic_prev, DELETE_ITEM, NULL);
            }
        }

      /* if this is an itemp & result of an address of a true sym
         then mark this as rematerialisable   */
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

      /* if straight assignment then carry remat flag if
         this is the only definition */
      if (ic->op == '=' &&
          !POINTER_SET (ic) &&
          IS_SYMOP (IC_RIGHT (ic)) &&
          OP_SYMBOL (IC_RIGHT (ic))->remat &&
          !IS_CAST_ICODE (OP_SYMBOL (IC_RIGHT (ic))->rematiCode) &&
          !isOperandGlobal (IC_RESULT (ic)) &&  /* due to bug 1618050 */
          bitVectnBitsOn (OP_SYMBOL (IC_RESULT (ic))->defs) <= 1 &&
          !OP_SYMBOL (IC_RESULT (ic))->addrtaken)
        {
          OP_SYMBOL (IC_RESULT (ic))->remat = OP_SYMBOL (IC_RIGHT (ic))->remat;
          OP_SYMBOL (IC_RESULT (ic))->rematiCode = OP_SYMBOL (IC_RIGHT (ic))->rematiCode;
        }

      /* if cast to a generic pointer & the pointer being
         cast is remat, then we can remat this cast as well */
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

      /* if this is a +/- operation with a rematerializable
         then mark this as rematerializable as well */
      if ((ic->op == '+' || ic->op == '-') &&
          IS_SYMOP (IC_LEFT (ic)) &&
          IS_ITEMP (IC_RESULT (ic)) &&
          IS_OP_LITERAL (IC_RIGHT (ic)) &&
          OP_SYMBOL (IC_LEFT (ic))->remat &&
          (!IS_SYMOP (IC_RIGHT (ic)) || !IS_CAST_ICODE (OP_SYMBOL (IC_RIGHT (ic))->rematiCode)) &&
          bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1)
        {
          OP_SYMBOL (IC_RESULT (ic))->remat = 1;
          OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
          OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
        }

      /* mark the pointer usages */
      if (POINTER_SET (ic) && IS_SYMOP (IC_RESULT (ic)))
        OP_SYMBOL (IC_RESULT (ic))->uptr = 1;

      if (POINTER_GET (ic) && IS_SYMOP (IC_LEFT (ic)))
        OP_SYMBOL (IC_LEFT (ic))->uptr = 1;

      if (!SKIP_IC2 (ic))
        {
          /* if we are using a symbol on the stack
             then we should say mcs251_ptrRegReq */
          if (options.useXstack && ic->parmPush && (ic->op == IPUSH || ic->op == IPOP))
            mcs251_ptrRegReq++;
          if (ic->op == IFX && IS_SYMOP (IC_COND (ic)))
            mcs251_ptrRegReq += ((OP_SYMBOL (IC_COND (ic))->onStack ||
                                 OP_SYMBOL (IC_COND (ic))->iaccess ||
                                 SPEC_OCLS (OP_SYMBOL (IC_COND (ic))->etype) == idata) ? 1 : 0);
          else if (ic->op == JUMPTABLE && IS_SYMOP (IC_JTCOND (ic)))
            mcs251_ptrRegReq += ((OP_SYMBOL (IC_JTCOND (ic))->onStack ||
                                 OP_SYMBOL (IC_JTCOND (ic))->iaccess ||
                                 SPEC_OCLS (OP_SYMBOL (IC_JTCOND (ic))->etype) == idata) ? 1 : 0);
          else
            {
              if (IS_SYMOP (IC_LEFT (ic)))
                mcs251_ptrRegReq += ((OP_SYMBOL (IC_LEFT (ic))->onStack ||
                                     OP_SYMBOL (IC_LEFT (ic))->iaccess ||
                                     SPEC_OCLS (OP_SYMBOL (IC_LEFT (ic))->etype) == idata) ? 1 : 0);
              if (IS_SYMOP (IC_RIGHT (ic)))
                mcs251_ptrRegReq += ((OP_SYMBOL (IC_RIGHT (ic))->onStack ||
                                     OP_SYMBOL (IC_RIGHT (ic))->iaccess ||
                                     SPEC_OCLS (OP_SYMBOL (IC_RIGHT (ic))->etype) == idata) ? 1 : 0);
              if (IS_SYMOP (IC_RESULT (ic)))
                mcs251_ptrRegReq += ((OP_SYMBOL (IC_RESULT (ic))->onStack ||
                                     OP_SYMBOL (IC_RESULT (ic))->iaccess ||
                                     SPEC_OCLS (OP_SYMBOL (IC_RESULT (ic))->etype) == idata) ? 1 : 0);
              if (POINTER_GET (ic) && IS_SYMOP (IC_LEFT (ic))
                  && getSize (OP_SYMBOL (IC_LEFT (ic))->type) <= (unsigned int) NEARPTRSIZE)
                mcs251_ptrRegReq++;
              if (POINTER_SET (ic) && IS_SYMOP (IC_RESULT (ic))
                  && getSize (OP_SYMBOL (IC_RESULT (ic))->type) <= (unsigned int) NEARPTRSIZE)
                mcs251_ptrRegReq++;
            }
        }

      /* if the condition of an if instruction
         is defined in the previous instruction and
         this is the only usage then
         mark the itemp as a conditional */
      if ((IS_CONDITIONAL (ic) ||
           (IS_BITWISE_OP (ic) && isBitwiseOptimizable (ic))) &&
          ic->next && ic->next->op == IFX &&
          bitVectnBitsOn (OP_USES (IC_RESULT (ic))) == 1 &&
          isOperandEqual (IC_RESULT (ic), IC_COND (ic->next)) && OP_SYMBOL (IC_RESULT (ic))->liveTo <= ic->next->seq)
        {
          OP_SYMBOL (IC_RESULT (ic))->regType = REG_CND;
          continue;
        }

      /* if the condition of an if instruction
         is defined in the previous GET_POINTER instruction and
         this is the only usage then
         mark the itemp as accumulator use */
      if ((POINTER_GET (ic) && getSize (operandType (IC_RESULT (ic))) <= 1) &&
          ic->next && ic->next->op == IFX &&
          bitVectnBitsOn (OP_USES (IC_RESULT (ic))) == 1 &&
          isOperandEqual (IC_RESULT (ic), IC_COND (ic->next)) && OP_SYMBOL (IC_RESULT (ic))->liveTo <= ic->next->seq)
        {
          OP_SYMBOL (IC_RESULT (ic))->accuse = 1;
          continue;
        }

      /* reduce for support function calls */
      if (ic->supportRtn || ic->op == '+' || ic->op == '-')
        packRegsForSupport (ic, ebp);

      /* some cases the redundant moves can
         can be eliminated for return statements */
      if ((ic->op == RETURN || (ic->op == SEND && ic->argreg == 1)) &&
          !isOperandInFarSpace (IC_LEFT (ic)) && (options.model == MODEL_SMALL || options.model == MODEL_MEDIUM))
        {
          packRegsForOneuse (ic, IC_LEFT (ic), ebp);
        }

      /* if pointer set & left has a size more than
         one and right is not in far space */
      if (POINTER_SET (ic) &&
          IS_SYMOP (IC_RESULT (ic)) &&
          !isOperandInFarSpace (IC_RIGHT (ic)) &&
          !OP_SYMBOL (IC_RESULT (ic))->remat &&
          !IS_OP_RUONLY (IC_RIGHT (ic)) && getSize (aggrToPtr (operandType (IC_RESULT (ic)), FALSE)) > 1)
        {
          packRegsForOneuse (ic, IC_RESULT (ic), ebp);
        }

      /* if pointer get */
      if (POINTER_GET (ic) &&
          IS_SYMOP (IC_LEFT (ic)) &&
          !isOperandInFarSpace (IC_RESULT (ic)) &&
          !OP_SYMBOL (IC_LEFT (ic))->remat &&
          !IS_OP_RUONLY (IC_RESULT (ic)) &&
          getSize (aggrToPtr (operandType (IC_LEFT (ic)), FALSE)) > 1)
        {
          packRegsForOneuse (ic, IC_LEFT (ic), ebp);
        }

      /* if this is a cast for integral promotion then
         check if it's the only use of the definition of the
         operand being casted/ if yes then replace
         the result of that arithmetic operation with
         this result and get rid of the cast */
      if (ic->op == CAST)
        {
          sym_link *fromType = operandType (IC_RIGHT (ic));
          sym_link *toType = operandType (IC_LEFT (ic));

          if (IS_INTEGRAL (fromType) && IS_INTEGRAL (toType) &&
              getSize (fromType) != getSize (toType) && SPEC_USIGN (fromType) == SPEC_USIGN (toType))
            {
              iCode *dic = packRegsForOneuse (ic, IC_RIGHT (ic), ebp);
              if (dic)
                {
                  if (IS_ARITHMETIC_OP (dic))
                    {
                      bitVectUnSetBit (OP_SYMBOL (IC_RESULT (dic))->defs, dic->key);
                      ReplaceOpWithCheaperOp (&IC_RESULT (dic), IC_RESULT (ic));
                      remiCodeFromeBBlock (ebp, ic);
                      bitVectUnSetBit (OP_SYMBOL (IC_RESULT (ic))->defs, ic->key);
                      hTabDeleteItem (&iCodehTab, ic->key, ic, DELETE_ITEM, NULL);
                      OP_DEFS (IC_RESULT (dic)) = bitVectSetBit (OP_DEFS (IC_RESULT (dic)), dic->key);
                      ic = ic->prev;
                    }
                  else
                    {
                      OP_SYMBOL (IC_RIGHT (ic))->ruonly = 0;
                    }
                }
            }
          else
            {
              /* if the type from and type to are the same
                 then if this is the only use then pack it */
              if (compareType (operandType (IC_RIGHT (ic)), operandType (IC_LEFT (ic)), false) == 1)
                {
                  iCode *dic = packRegsForOneuse (ic, IC_RIGHT (ic), ebp);
                  if (dic)
                    {
                      bitVectUnSetBit (OP_SYMBOL (IC_RESULT (dic))->defs, dic->key);
                      ReplaceOpWithCheaperOp (&IC_RESULT (dic), IC_RESULT (ic));
                      remiCodeFromeBBlock (ebp, ic);
                      bitVectUnSetBit (OP_SYMBOL (IC_RESULT (ic))->defs, ic->key);
                      hTabDeleteItem (&iCodehTab, ic->key, ic, DELETE_ITEM, NULL);
                      OP_DEFS (IC_RESULT (dic)) = bitVectSetBit (OP_DEFS (IC_RESULT (dic)), dic->key);
                      ic = ic->prev;
                    }
                }
            }
        }

      /* pack for PUSH
         iTempNN := (some variable in farspace) V1
         push iTempNN ;
         -------------
         push V1
       */
      if (ic->op == IPUSH)
        {
          packForPush (ic, ebpp, blockno);
        }

      /* pack registers for accumulator use, when the
         result of an arithmetic or bit wise operation
         has only one use, that use is immediately following
         the definition and the using iCode has only one
         operand or has two operands but one is literal &
         the result of that operation is not on stack then
         we can leave the result of this operation in acc:b
         combination */
      if ((IS_ARITHMETIC_OP (ic)
           || IS_CONDITIONAL (ic)
           || IS_BITWISE_OP (ic)
           || ic->op == LEFT_OP || ic->op == RIGHT_OP || ic->op == CALL
           || ic->op == '=' && !POINTER_SET(ic) && getSize (operandType (IC_RESULT (ic))) < 2
           || (ic->op == ADDRESS_OF && isOperandOnStack (IC_LEFT (ic)))) &&
          IS_ITEMP (IC_RESULT (ic)) && getSize (operandType (IC_RESULT (ic))) <= 2)
        {
          packRegsForAccUse (ic);
        }
    }
}


void
mcs251_ralloc2_prepare (ebbIndex * ebbi)
{
  int i;

  for (i = 0; i < ebbi->count; ++i)
    packRegisters (ebbi->bbOrder, i);
}


