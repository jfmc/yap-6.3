/*************************************************************************
*									 *
*	 YAP Prolog 							 *
*									 *
*	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
*									 *
* Copyright L.Damas, V.S.Costa and Universidade do Porto 1985-1997	 *
*									 *
**************************************************************************
*									 *
* File:		exec.c							 *
* Last rev:	8/2/88							 *
* mods:									 *
* comments:	Execute Prolog code					 *
*									 *
*************************************************************************/
#ifdef SCCS
static char     SccsId[] = "@(#)cdmgr.c	1.1 05/02/98";
#endif

#include "absmi.h"
#include "yapio.h"

STATIC_PROTO(Int  CallPredicate, (PredEntry *, choiceptr));
STATIC_PROTO(Int  CallClause, (PredEntry *, unsigned int, Int));
STATIC_PROTO(Int  p_save_cp, (void));
STATIC_PROTO(Int  p_execute, (void));
STATIC_PROTO(Int  p_execute0, (void));
STATIC_PROTO(Int  p_at_execute, (void));

static Term
current_cp_as_integer(void)
{
  return(MkIntTerm(LCL0-(CELL *)B));
}

static inline Int
CallPredicate(PredEntry *pen, choiceptr cut_pt) {
  WRITE_LOCK(pen->PRWLock);
#ifdef DEPTH_LIMIT
  if (DEPTH <= MkIntTerm(1)) {/* I assume Module==0 is prolog */
    if (pen->ModuleOfPred) {
      if (DEPTH == MkIntTerm(0))
	return(FALSE);
      else DEPTH = RESET_DEPTH();
    }
  } else if (pen->ModuleOfPred)
    DEPTH -= MkIntConstant(2);
#endif	/* DEPTH_LIMIT */
#ifdef LOW_LEVEL_TRACER
  if (do_low_level_trace)
    low_level_trace(enter_pred,pen,XREGS+1);
#endif	/* LOW_LEVEL_TRACE */
  CP = P;
  P = (yamop *)(pen->CodeOfPred);
  WRITE_UNLOCK(pen->PRWLock);
  ENV = YENV;
  YENV = ASP;
  YENV[E_CB] = (CELL) cut_pt;
  return (TRUE);
}

inline static Int
CallMetaCall(SMALLUNSGN mod) {
  ARG2 = current_cp_as_integer(); /* p_save_cp */
  ARG3 = ARG1;
  ARG4 = ModuleName[mod];
  return (CallPredicate(PredMetaCall, B));
}

Term
ExecuteCallMetaCall(SMALLUNSGN mod) {
  Term ts[4];
  ts[0] = ARG1;
  ts[1] = current_cp_as_integer(); /* p_save_cp */
  ts[2] = ARG1;
  ts[3] = ModuleName[mod];
  return(MkApplTerm(PredMetaCall->FunctorOfPred,4,ts));
}

static Int
CallError(yap_error_number err, SMALLUNSGN mod)
{
  if (yap_flags[LANGUAGE_MODE_FLAG] == 1) {
    return(CallMetaCall(mod));
  } else {
    Error(err, ARG1, "call/1");
    return(FALSE);
  }
}

static Int 
CallClause(PredEntry *pen, unsigned int arity, Int position)
{
  CELL            flags;

  if (position == -1) return(CallPredicate(pen, B));
  WRITE_LOCK(pen->PRWLock);
  flags = pen->PredFlags;
  if ((flags & (CompiledPredFlag | DynamicPredFlag)) ||
      pen->OpcodeOfPred == UNDEF_OPCODE) {
    CODEADDR        q;
#ifdef DEPTH_LIMIT
    if (DEPTH <= MkIntTerm(1)) {/* I assume Module==0 is prolog */
      if (pen->ModuleOfPred) {
	if (DEPTH == MkIntTerm(0))
	  return(FALSE);
	else DEPTH = RESET_DEPTH();
      }
    } else if (pen->ModuleOfPred)
      DEPTH -= MkIntConstant(2);
#endif	/* DEPTH_LIMIT */
#ifdef LOW_LEVEL_TRACER
    if (do_low_level_trace)
      low_level_trace(enter_pred,pen,XREGS+1);
#endif	/* LOW_LEVEL_TRACE */
    ENV = YENV;
    YENV = ASP;
    YENV[E_CB] = (CELL)(B->cp_b);
    CP = P;
    q = pen->FirstClause;
    if (pen->PredFlags & ProfiledPredFlag) {
      LOCK(pen->StatisticsForPred.lock);
      if (position == 1)
	pen->StatisticsForPred.NOfEntries++;
      else
	pen->StatisticsForPred.NOfRetries++;
      UNLOCK(pen->StatisticsForPred.lock);
    }
    if (flags & DynamicPredFlag) {
      CLAUSECODE->arity = pen->ArityOfPE;
      CLAUSECODE->func = pen->FunctorOfPred;
      while (position > 1) {
	while (ClauseCodeToClause(q)->ClFlags & ErasedMask)
	  q = NextClause(q);
	position--;
	q = NextClause(q);
      }
      while (ClauseCodeToClause(q)->ClFlags & ErasedMask)
	q = NextClause(q);
#if defined(YAPOR) || defined(THREADS)
      {
	Clause *cl = ClauseCodeToClause(q);
	
	LOCK(cl->ClLock);
	TRAIL_CLREF(cl);
	INC_DBREF_COUNT(cl);
	UNLOCK(cl->ClLock);
      }
#else 
      if (!(ClauseCodeToClause(q)->ClFlags & InUseMask)) {
	OPREG     *opp = &(ClauseCodeToClause(q)->ClFlags);
	TRAIL_CLREF(ClauseCodeToClause(q));
	*opp |= InUseMask;
      }
#endif
      CLAUSECODE->clause = (CODEADDR)NEXTOP((yamop *)(q),ld);
      P = (yamop *)CLAUSECODE->clause;
      WRITE_UNLOCK(pen->PRWLock);
      return((CELL)(&(CLAUSECODE->clause)));
    } else {
      for (; position > 1; position--)
	q = NextClause(q);
      P = NEXTOP((yamop *)(q),ld);
      WRITE_UNLOCK(pen->PRWLock);
      return (Unsigned(pen));
    }
  } else {
    Error(SYSTEM_ERROR,ARG1,"debugger tries to debug clause for builtin");    
    return (FALSE);
  }
}

static Int
p_save_cp(void)
{
  Term t = Deref(ARG1);
  Term td;
#if SHADOW_HB
  register CELL *HBREG = HB;
#endif
  if (!IsVarTerm(t)) return(FALSE);
  td = current_cp_as_integer();
  BIND((CELL *)t,td,bind_save_cp);
#ifdef COROUTINING
  DO_TRAIL(CellPtr(t), td);
  if (CellPtr(t) < H0) WakeUp((CELL *)t);
 bind_save_cp:
#endif
  return(TRUE);
}

static Int
EnterCreepMode(SMALLUNSGN mod) {
  PredEntry *PredSpy = RepPredProp(PredPropByFunc(FunctorSpy,0));
  Term tn = MkApplTerm(MkFunctor(AtomMetaCall,1),1,&ARG1);
  ARG1 = MkPairTerm(ModuleName[mod],tn);
  CreepFlag = CalculateStackGap();
  P_before_spy = P;
  return (CallPredicate(PredSpy, B));
}

inline static Int
do_execute(Term t, SMALLUNSGN mod)
{
  if (yap_flags[SPY_CREEP_FLAG]) {
    return(EnterCreepMode(mod));
  } else if (PredGoalExpansion->OpcodeOfPred != UNDEF_OPCODE) {
    return(CallMetaCall(mod));
  }
 restart_exec:
  if (IsVarTerm(t)) {
    return CallError(INSTANTIATION_ERROR, mod);
  } else if (IsApplTerm(t)) {
    register Functor f = FunctorOfTerm(t);
    register CELL *pt;
    PredEntry *pen;
    unsigned int i, arity;

    f = FunctorOfTerm(t);
    if (IsExtensionFunctor(f)) {
      return CallError(TYPE_ERROR_CALLABLE, mod);
    }
    arity = ArityOfFunctor(f);
    
    pen = RepPredProp(PredPropByFunc(f, mod));
    /* You thought we would be over by now */
    /* but no meta calls require special preprocessing */
    if (pen->PredFlags & MetaPredFlag) {
      if (f == FunctorModule) {
	Term tmod = ArgOfTerm(1,t);
	if (!IsVarTerm(tmod) && IsAtomTerm(tmod)) {
	  mod = LookupModule(tmod);
	  t = ArgOfTerm(2,t);
	  goto restart_exec;
	}
      }
      return(CallMetaCall(mod));
    }
    /* now let us do what we wanted to do from the beginning !! */
    /* I cannot use the standard macro here because
       otherwise I would dereference the argument and
       might skip a svar */
    pt = RepAppl(t)+1;
    for (i = 1; i <= arity; i++) {
#if SBA
      Term d0 = *pt++;
      if (d0 == 0)
	XREGS[i] = (CELL)(pt-1);
      else
	XREGS[i] = d0;
#else
      XREGS[i] = *pt++;
#endif
    }
    return (CallPredicate(pen, B));
  } else if (IsAtomTerm(t)) { 
    PredEntry            *pe;
    Atom a = AtomOfTerm(t);

    if (a == AtomTrue || a == AtomOtherwise || a == AtomCut)
      return(TRUE);
    else if (a == AtomFail || a == AtomFalse)
      return(FALSE);
    /* call may not define new system predicates!! */
    pe = RepPredProp(PredPropByAtom(a, mod));
    return (CallPredicate(pe, B));
  } else if (IsIntTerm(t)) {
    return CallError(TYPE_ERROR_CALLABLE, mod);
  } else {
    /* Is Pair Term */
    return(CallMetaCall(mod));
  }
}

static Int
p_execute(void)
{				/* '$execute'(Goal)	 */
  Term            t = Deref(ARG1);
  return(do_execute(t, CurrentModule));
}

static Int
p_execute_in_mod(void)
{				/* '$execute'(Goal)	 */
  return(do_execute(Deref(ARG1), IntOfTerm(Deref(ARG2))));
}

inline static Int
CallMetaCallWithin(void)
{
  return (CallPredicate(PredMetaCall, B));
}

/* '$execute_within'(Goal,CutPt,OrigGoal,Mod)	 */
static Int
p_execute_within(void)
{ 
  Term            t = Deref(ARG1);
  Term            tmod = Deref(ARG4);
  unsigned int    arity;
  Prop            pe;
  Atom            a;
  SMALLUNSGN      mod = LookupModule(tmod);
#ifdef SBA
  choiceptr cut_pt = (choiceptr)IntegerOfTerm(Deref(ARG2));
#else
  choiceptr cut_pt = (choiceptr)(LCL0-IntegerOfTerm(Deref(ARG2)));
#endif

 restart_exec:
  if (yap_flags[SPY_CREEP_FLAG]) {
    return(EnterCreepMode(mod));
  } else  if (PredGoalExpansion->OpcodeOfPred != UNDEF_OPCODE) {
    return(CallMetaCallWithin());
    /* at this point check if we should enter creep mode */ 
  } else  if (IsVarTerm(t)) {
    return CallError(INSTANTIATION_ERROR, mod);
  } else if (IsApplTerm(t)) {
    register Functor f = FunctorOfTerm(t);
    register unsigned int    i;
    register CELL *pt;

    if (IsExtensionFunctor(f)) {
      return CallError(TYPE_ERROR_CALLABLE, mod);
    }
    
    {
      PredEntry *pen;
      arity = ArityOfFunctor(f);
      a = NameOfFunctor(f);

      pe = PredPropByFunc(f, mod);
      pen = RepPredProp(pe);
      /* You thought we would be over by now */
      /* but no meta calls require special preprocessing */
      if (pen->PredFlags & MetaPredFlag) {
	if (f == FunctorModule) {
	  Term tmod = ArgOfTerm(1,t);
	  if (!IsVarTerm(tmod) && IsAtomTerm(tmod)) {
	    mod = LookupModule(tmod);
	    t = ArgOfTerm(2,t);
	    goto restart_exec;
	  }
	}
	return(CallMetaCallWithin());
      }
      /* now let us do what we wanted to do from the beginning !! */
      /* I cannot use the standard macro here because
	 otherwise I would dereference the argument and
	 might skip a svar */
      pt = RepAppl(t)+1;
      for (i = 1; i <= arity; ++i) {
#if SBA
	Term d0 = *pt++;
	if (d0 == 0)
	  XREGS[i] = (CELL)(pt-1);
	else
	  XREGS[i] = d0;
#else
	XREGS[i] = *pt++;
#endif
      }
      return (CallPredicate(pen, cut_pt));
    }
  } else if (IsAtomOrIntTerm(t)) {
    if (IsIntTerm(t)) {
      return CallError(TYPE_ERROR_CALLABLE, mod);
    }    
    a = AtomOfTerm(t);
    if (a == AtomTrue || a == AtomOtherwise)
      return(TRUE);
    else if (a == AtomCut) {
      /* find where to cut to */
      if (SHOULD_CUT_UP_TO(B,cut_pt)) {
#ifdef YAPOR
	/* Wow, we're gonna cut!!! */
	CUT_prune_to(cut_pt);
#else
	/* Wow, we're gonna cut!!! */
	B = cut_pt;
#endif /* YAPOR */
#ifdef TABLING
        abolish_incomplete_subgoals(B);
#endif /* TABLING */
	HB = PROTECT_FROZEN_H(B);
      }
      return(TRUE);
    } else if (a == AtomFail || a == AtomFalse) {
      return(FALSE);
    } else {
      /* call may not define new system predicates!! */
      pe = PredPropByAtom(a, mod);
      return (CallPredicate(RepPredProp(pe), cut_pt));
    }
  } else {
    /* Is Pair Term */
    return(CallMetaCallWithin());
  }
}

/* '$execute_within2'(Goal)	 */
static Int
p_execute_within2(void)
{ 
  Term            t = Deref(ARG1);
  Prop            pe;
  SMALLUNSGN mod = CurrentModule;

 restart_exec:
  if (yap_flags[SPY_CREEP_FLAG]) {
    return(EnterCreepMode(CurrentModule));
  } else if (PredGoalExpansion->OpcodeOfPred != UNDEF_OPCODE) {
    return(CallMetaCallWithin());
  } else  if (IsVarTerm(t)) {
    return CallError(INSTANTIATION_ERROR, CurrentModule);
  } else if (IsApplTerm(t)) {
    register Functor f = FunctorOfTerm(t);

    if (IsExtensionFunctor(f)) {
      return CallError(TYPE_ERROR_CALLABLE, mod);
    }
    if (f == FunctorModule) {
      Term tmod = ArgOfTerm(1,t);
      if (!IsVarTerm(tmod) && IsAtomTerm(tmod)) {
	mod = LookupModule(tmod);
	t = ArgOfTerm(2,t);
	goto restart_exec;
      }
    }
    
    {
      PredEntry *pen;
      CELL *dest;
      register CELL *pt;
      register unsigned int    i;
      unsigned int arity = ArityOfFunctor(f);

      pe = PredPropByFunc(f, mod);
      pen = RepPredProp(pe);
      /* You thought we would be over by now */
      /* but no meta calls require special preprocessing */
      if (pen->PredFlags & MetaPredFlag) {
	return(CallMetaCallWithin());
      }
      /* at this point check if we should enter creep mode */ 
      /* now let us do what we wanted to do from the beginning !! */
      /* I cannot use the standard macro here because
	 otherwise I would dereference the argument and
	 might skip a svar */
      pt = RepAppl(t)+1;
      dest = XREGS+1;
      for (i = 0; i < arity; ++i) {
#if SBA
	Term d0 = *pt++;
	if (d0 == 0)
	  *dest++ = (CELL)(pt-1);
	else
	  *dest++ = d0;
#else
	*dest++ = *pt++;
#endif
      }
      if (pen->PredFlags & CutTransparentPredFlag)
	return (CallPredicate(pen, (choiceptr)(ENV[E_CB])));
      else
	return (CallPredicate(pen, B));
    }
  } else if (IsAtomTerm(t)) {
    Atom a = AtomOfTerm(t);
    
    if (a == AtomTrue || a == AtomOtherwise)
      return(TRUE);
    else if (a == AtomCut) {
      choiceptr pt0;

      pt0 = (choiceptr)(ENV[E_CB]);
      /* find where to cut to */
      if (SHOULD_CUT_UP_TO(B,pt0)) {
#ifdef YAPOR
	/* Wow, we're gonna cut!!! */
	CUT_prune_to(pt0);
#else
	/* Wow, we're gonna cut!!! */
	B = pt0;
#endif /* YAPOR */
#ifdef TABLING
        abolish_incomplete_subgoals(B);
#endif /* TABLING */
	HB = PROTECT_FROZEN_H(B);
      }
      return(TRUE);
    } else if (a == AtomFail || a == AtomFalse) {
      return(FALSE);
    }
    /* call may not define new system predicates!! */
    pe = PredPropByAtom(a, CurrentModule);
    return (CallPredicate(RepPredProp(pe), B));
  } else if (IsIntTerm(t)) {
    return CallError(TYPE_ERROR_CALLABLE, mod);
  } else {
    /* Is Pair Term */
    return(CallMetaCallWithin());
  }
}


static Int
p_execute0(void)
{				/* '$execute0'(Goal,Mod)	 */
  Term            t = Deref(ARG1);
  Term            tmod = Deref(ARG2);
  unsigned int    arity;
  Prop            pe;
  SMALLUNSGN      mod = LookupModule(tmod);

 restart_exec:
  if (IsVarTerm(t)) {
    Error(INSTANTIATION_ERROR,ARG3,"call/1");    
    return(FALSE);
  } else if (IsAtomTerm(t)) {
    Atom a = AtomOfTerm(t);
    pe = PredPropByAtom(a, mod);
  } else if (IsApplTerm(t)) {
    register Functor f = FunctorOfTerm(t);
    register unsigned int    i;
    register CELL *pt;

    if (IsExtensionFunctor(f))
      return(FALSE);
    if (f == FunctorModule) {
      Term tmod = ArgOfTerm(1,t);
      if (!IsVarTerm(tmod) && IsAtomTerm(tmod)) {
	mod = LookupModule(tmod);
	t = ArgOfTerm(2,t);
	goto restart_exec;
      }
    }
    arity = ArityOfFunctor(f);
    /* I cannot use the standard macro here because
       otherwise I would dereference the argument and
       might skip a svar */
    pt = RepAppl(t)+1;
    for (i = 1; i <= arity; ++i) {
#if SBA
	Term d0 = *pt++;
	if (d0 == 0)
	  XREGS[i] = (CELL)(pt-1);
	else
	  XREGS[i] = d0;
#else
      XREGS[i] = *pt++;
#endif
    }
    pe = PredPropByFunc(f, mod);
  } else {
    Error(TYPE_ERROR_CALLABLE,ARG3,"call/1");    
    return(FALSE);
  }
  /*	N = arity; */
  /* call may not define new system predicates!! */
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_0(void)
{				/* '$execute_0'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG2));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  pe = PredPropByAtom(a, mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_1(void)
{				/* '$execute_0'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG3));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  pe = PredPropByFunc(MkFunctor(a,1),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_2(void)
{				/* '$execute_2'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG4));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  pe = PredPropByFunc(MkFunctor(a, 2),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_3(void)
{				/* '$execute_3'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG5));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  pe = PredPropByFunc(MkFunctor(a, 3),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_4(void)
{				/* '$execute_4'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG6));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  pe = PredPropByFunc(MkFunctor(a, 4),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_5(void)
{				/* '$execute_5'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG7));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  ARG5 = ARG6;
  pe = PredPropByFunc(MkFunctor(a, 5),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_6(void)
{				/* '$execute_6'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG8));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  ARG5 = ARG6;
  ARG6 = ARG7;
  pe = PredPropByFunc(MkFunctor(a, 6),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_7(void)
{				/* '$execute_7'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG9));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  ARG5 = ARG6;
  ARG6 = ARG7;
  ARG7 = ARG8;
  pe = PredPropByFunc(MkFunctor(a, 7),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_8(void)
{				/* '$execute_8'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG10));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  ARG5 = ARG6;
  ARG6 = ARG7;
  ARG7 = ARG8;
  ARG8 = ARG9;
  pe = PredPropByFunc(MkFunctor(a, 8),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_9(void)
{				/* '$execute_9'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG11));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  ARG5 = ARG6;
  ARG6 = ARG7;
  ARG7 = ARG8;
  ARG8 = ARG9;
  ARG9 = ARG10;
  pe = PredPropByFunc(MkFunctor(a, 9),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

static Int
p_execute_10(void)
{				/* '$execute_10'(Goal)	 */
  Term            t = Deref(ARG1);
  SMALLUNSGN      mod = LookupModule(Deref(ARG12));
  Prop            pe;
  Atom            a;

  a = AtomOfTerm(t);
  ARG1 = ARG2;
  ARG2 = ARG3;
  ARG3 = ARG4;
  ARG4 = ARG5;
  ARG5 = ARG6;
  ARG6 = ARG7;
  ARG7 = ARG8;
  ARG8 = ARG9;
  ARG9 = ARG10;
  ARG10 = ARG11;
  pe = PredPropByFunc(MkFunctor(a, 10),mod);
  return (CallPredicate(RepPredProp(pe), B));
}

#ifdef DEPTH_LIMIT
static Int
p_execute_depth_limit(void) {
  Term d = Deref(ARG2);
  if (IsVarTerm(d)) {
    Error(INSTANTIATION_ERROR,d,"depth_bound_call/2");    
  } else if (!IsIntTerm(d)) {
    Error(TYPE_ERROR_INTEGER, d, "depth_bound_call/2");
    return(FALSE);
  }
  DEPTH = MkIntTerm(IntOfTerm(d)*2);
  return(p_execute());
}
#endif

static Int
p_pred_goal_expansion_on(void) {
  /* a goal needs expansion if we have goal_expansion defined or
     if the goal is a meta-call */
  return (PredGoalExpansion->OpcodeOfPred != UNDEF_OPCODE);
}

static Int 
p_at_execute(void)
{				/* '$execute'(Goal,ClauseNumber) */
  Term            t = Deref(ARG1), tmod = Deref(ARG2), t2 = Deref(ARG3);
  unsigned	  int arity;
  Prop            pe;
  Atom            a;
  SMALLUNSGN      mod = LookupModule(tmod);

 restart_exec:
  if (IsAtomTerm(t)) {
    a = AtomOfTerm(t);
    pe = PredPropByAtom(a, mod);
    arity = 0;
  } else if (IsApplTerm(t)) {
    register Functor f = FunctorOfTerm(t);
    register unsigned int    i;
    register CELL *pt;

    if (IsBlobFunctor(f))
      return(FALSE);
    if (f == FunctorModule) {
      Term tmod = ArgOfTerm(1,t);
      if (!IsVarTerm(tmod) && IsAtomTerm(tmod)) {
	mod = LookupModule(tmod);
	t = ArgOfTerm(2,t);
	goto restart_exec;
      }
      if (IsVarTerm(tmod)) {
	Error(INSTANTIATION_ERROR, ARG1, "calling clause in debugger");
      }
      Error(TYPE_ERROR_ATOM, ARG1, "calling clause in debugger");
    }
    arity = ArityOfFunctor(f);
    a = NameOfFunctor(f);
    /* I cannot use the standard macro here because
       otherwise I would dereference the argument and
       might skip a svar */
    pt = RepAppl(t)+1;
    for (i = 1; i <= arity; ++i)
#if SBA
      {
	Term d0 = *pt++;
	if (d0 == 0)
	  XREGS[i] = (CELL)(pt-1);
	else
	  XREGS[i] = d0;
      }
#else
      XREGS[i] = *pt++;
#endif
      pe = PredPropByFunc(f,mod);
  } else
    return (FALSE);	/* for the moment */
  if (IsVarTerm(t2) || !IsIntTerm(t2))
    return (FALSE);
  /* N = arity; */
  /* call may not define new system predicates!! */
  return (CallClause(RepPredProp(pe), arity, IntOfTerm(t2)));
}

int
exec_absmi(int top)
{
  int lval;
  if (top && (lval = sigsetjmp (RestartEnv, 1)) != 0) {
    if (lval == 1) { /* restart */
      /* otherwise, SetDBForThrow will fail entering critical mode */
      PrologMode = UserMode;
      /* find out where to cut to */
#if defined(__GNUC__)
#if defined(hppa) || defined(__alpha)
     /* siglongjmp resets the TR hardware register */
      restore_TR();
#endif
#if defined(__alpha)
     /* siglongjmp resets the H hardware register */
      restore_H();
#endif
#endif
      yap_flags[SPY_CREEP_FLAG] = 0;
      CreepFlag = CalculateStackGap();
      P = (yamop *)FAILCODE;
    }
    if (lval == 2) { /* arithmetic exception */
      /* must be done here, otherwise siglongjmp will clobber all the registers */
      Error(YAP_matherror,TermNil,NULL);
      /* reset the registers so that we don't have trash in abstract machine */
      set_fpu_exceptions(yap_flags[LANGUAGE_MODE_FLAG] == 1);
      P = (yamop *)FAILCODE;
    }
    if (lval == 3) { /* saved state */
      return(FALSE);
    }
  }
  PrologMode = UserMode;
  return(absmi(0));
}

static int
do_goal(CODEADDR CodeAdr, int arity, CELL *pt, int args_to_save, int top)
{
  choiceptr saved_b = B;

  /* create an initial pseudo environment so that when garbage
     collection is going up in the environment chain it doesn't get
     confused */
  EX = 0L;
  YENV = ASP;
  YENV[E_CP] = (CELL)P;
  YENV[E_CB] = (CELL)B;
  YENV[E_E]  = (CELL)ENV;
#ifdef  DEPTH_LIMIT
  YENV[E_DEPTH] = DEPTH;
#endif
  ENV = YENV;
  ASP -= EnvSizeInCells;
  /* and now create a pseudo choicepoint for much the same reasons */
  /* CP = YESCODE; */
  /* keep a place where you can inform you had an exception */
  { 
    int i;
    for (i = 0; i < arity; i++) {
      XREGS[i+1] = *pt++;
    }
  }
  B = (choiceptr)ASP;
  B--;
#ifdef TABLING
  if (top) {
    DepFr_cons_cp(GLOBAL_root_dep_fr) = B;
  }
#endif /* TABLING */
  B->cp_h     = H;
  B->cp_tr    = TR;
  B->cp_cp    = CP;
  B->cp_ap    = NOCODE;
  B->cp_env   = ENV;
  B->cp_b     = saved_b;
#ifdef DEPTH_LIMIT
  B->cp_depth = DEPTH;
#endif /* DEPTH_LIMIT */
  YENV = ASP = (CELL *)B;
  HB = H;
  YENV[E_CB] = Unsigned (B);
  P = (yamop *) CodeAdr;
  CP = YESCODE;
  S = CellPtr (RepPredProp (PredPropByFunc (MkFunctor(AtomCall, 1),0)));	/* A1 mishaps */

 return(exec_absmi(top));
}


Int
execute_goal(Term t, int nargs, SMALLUNSGN mod)
{
  Int             out;
  CODEADDR        CodeAdr;
  yamop *saved_p, *saved_cp;
  Prop pe;
  PredEntry *ppe;
  CELL *pt;
  /* preserve the current restart environment */
  /* visualc*/
  /* just keep the difference because of possible garbage collections */


  saved_p = P;
  saved_cp = CP;
  
  if (IsAtomTerm(t)) {
    Atom a = AtomOfTerm(t);
    pt = NULL;
    pe = PredPropByAtom(a, mod);
  } else if (IsApplTerm(t)) {
    Functor f = FunctorOfTerm(t);

    if (IsBlobFunctor(f)) {
      Error(TYPE_ERROR_CALLABLE,t,"call/1");
      return(FALSE);
    }
    /* I cannot use the standard macro here because
       otherwise I would dereference the argument and
       might skip a svar */
    pt = RepAppl(t)+1;
    pe = PredPropByFunc(f, mod);
  } else {
    Error(TYPE_ERROR_CALLABLE,t,"call/1");
    return(FALSE);
  }
  ppe = RepPredProp(pe);
  if (pe == NIL) {
    return(CallMetaCall(mod));
  }
  READ_LOCK(ppe->PRWLock);
  if (IsAtomTerm(t)) {
    CodeAdr = RepPredProp (pe)->CodeOfPred;
    READ_UNLOCK(ppe->PRWLock);
    out = do_goal(CodeAdr, 0, pt, nargs, FALSE);
  } else {
    Functor f = FunctorOfTerm(t);
    CodeAdr = RepPredProp (pe)->CodeOfPred;
    READ_UNLOCK(ppe->PRWLock);
    out = do_goal(CodeAdr, ArityOfFunctor(f), pt, nargs, FALSE);
  }

  if (out == 1) {
    choiceptr old_B;
    /* we succeeded, let's prune */
    /* restore the old environment */
    /* get to previous environment */
#ifdef YAPOR
    CUT_prune_to((choiceptr)(ENV[E_CB]));
#else
    B = (choiceptr)(ENV[E_CB]);
#endif /* YAPOR */
#ifdef TABLING
    abolish_incomplete_subgoals(B);
#endif /* TABLING */
    /* find out where we have the old arguments */
    old_B = ((choiceptr)(ENV-(EnvSizeInCells+nargs+1)))-1;
    CP   = saved_cp;
    P    = saved_p;
    ASP  = ENV;
    *--ASP = MkIntTerm(0);
#ifdef DEPTH_LIMIT
    DEPTH= ENV[E_DEPTH];
#endif
    ENV  = (CELL *)(ENV[E_E]);
    /* we have failed, and usually we would backtrack to this B,
       trouble is, we may also have a delayed cut to do */
    if (B != NULL)
      HB   = B->cp_h;
    YENV = ENV;
    return(TRUE);
  } else if (out == 0) {
    ASP  = B->cp_env;
    P    = saved_p;
    CP   = saved_cp;
    H    = B->cp_h;
#ifdef DEPTH_LIMIT
    DEPTH= B->cp_depth;
#endif
    YENV= ASP = B->cp_env;
    ENV  = (CELL *)((B->cp_env)[E_E]);
    B    = B->cp_b;
    SET_BB(B);
    HB = PROTECT_FROZEN_H(B);
    return(FALSE);
  } else {
    Error(SYSTEM_ERROR,TermNil,"emulator crashed");
    return(FALSE);
  }
}

void
trust_last(void)
{
  ASP  = B->cp_env;
  P    = (yamop *)(B->cp_env[E_CP]);
  CP   = B->cp_cp;
  H    = B->cp_h;
#ifdef DEPTH_LIMIT
  DEPTH= B->cp_depth;
#endif
  YENV= ASP = B->cp_env;
  ENV  = (CELL *)((B->cp_env)[E_E]);
  B    = B->cp_b;
  if (B) {
    SET_BB(B);
    HB = PROTECT_FROZEN_H(B);
  }
}

int
RunTopGoal(Term t)
{
  CODEADDR        CodeAdr;
  Prop pe;
  PredEntry *ppe;
  CELL *pt;
  UInt arity;
  SMALLUNSGN mod = CurrentModule;
  int goal_out = 0;

 restart_runtopgoal:
  if (IsAtomTerm(t)) {
    Atom a = AtomOfTerm(t);
    pt = NULL;
    pe = PredPropByAtom(a, CurrentModule);
    arity = 0;
  } else if (IsApplTerm(t)) {
    Functor f = FunctorOfTerm(t);

    if (IsBlobFunctor(f)) {
      Error(TYPE_ERROR_CALLABLE,t,"call/1");
      return(FALSE);
    }
    if (f == FunctorModule) {
      Term tmod = ArgOfTerm(1,t);
      if (!IsVarTerm(tmod) && IsAtomTerm(tmod)) {
	mod = LookupModule(tmod);
	t = ArgOfTerm(2,t);
	goto restart_runtopgoal;
      }
    }
    /* I cannot use the standard macro here because
       otherwise I would dereference the argument and
       might skip a svar */
    pe = GetPredPropByFunc(f, CurrentModule);
    pt = RepAppl(t)+1;
    arity = ArityOfFunctor(f); 
  } else {
    Error(TYPE_ERROR_CALLABLE,t,"call/1");
    return(FALSE);
  }
  ppe = RepPredProp(pe);
  if (pe != NIL) {
    READ_LOCK(ppe->PRWLock);
  }
  if (pe == NIL) {
    if (pe != NIL) {
      READ_UNLOCK(ppe->PRWLock);
    }
    /* we must always start the emulator with Prolog code */
    return(FALSE);
  }
  CodeAdr = ppe->CodeOfPred;
  if (TrailTop - HeapTop < 2048) {
    PrologMode = BootMode;
    Error(SYSTEM_ERROR,TermNil,
	  "unable to boot because of too little heap space");
  }
  goal_out = do_goal(CodeAdr, arity, pt, 0, TRUE);
  return(goal_out);
}

static void
restore_regs(Term t)
{
  if (IsApplTerm(t)) {
    Int i;
    Int max = ArityOfFunctor(FunctorOfTerm(t));
    CELL *ptr = RepAppl(t)+1;

    for (i = 0; i < max; i += 2) {
      Int j = IntOfTerm(ptr[0]);
      XREGS[j] = ptr[1];
      ptr+=2;
    }
  }
}

/* low level voodoo to restore temporary registers after a call */
static Int
p_restore_regs(void)
{
  Term t = Deref(ARG1);
  if (IsVarTerm(t)) {
    Error(INSTANTIATION_ERROR,t,"support for coroutining");    
    return(FALSE);
  }
  if (IsAtomTerm(t)) return(TRUE);
  restore_regs(t);
  return(TRUE);
}

/* low level voodoo to cut and then restore temporary registers after a call */
static Int
p_restore_regs2(void)
{

  Term t = Deref(ARG1), d0;
  choiceptr pt0;
  if (IsVarTerm(t)) {
    Error(INSTANTIATION_ERROR,t,"support for coroutining");    
    return(FALSE);
  }
  if (!IsAtomTerm(t)) {
    restore_regs(t);
  }
  d0 = Deref(ARG2);
  if (IsVarTerm(d0)) {
    Error(INSTANTIATION_ERROR,d0,"support for coroutining");    
    return(FALSE);
  }
#if SBA
  if (!IsIntegerTerm(d0)) {
#else
  if (!IsIntTerm(d0)) {
#endif
    return(FALSE);
  }
#if SBA
  pt0 = (choiceptr)IntegerOfTerm(d0);
#else
  pt0 = (choiceptr)(LCL0-IntOfTerm(d0));
#endif
  /* find where to cut to */
  if (pt0 > B) {
    /* Wow, we're gonna cut!!! */
#ifdef YAPOR
    CUT_prune_to(pt0);
#else
    B = pt0;
#endif /* YAPOR */
#ifdef TABLING
    abolish_incomplete_subgoals(B);
#endif /* TABLING */
    HB = B->cp_h;
    /*    trim_trail();*/
  }
  return(TRUE);
}

static Int
p_clean_ifcp(void) {
#if SBA
  choiceptr pt0 = (choiceptr)IntegerOfTerm(Deref(ARG1));
#else
  choiceptr pt0 = (choiceptr)(LCL0-IntOfTerm(Deref(ARG1)));
#endif
  if (pt0 == B) {
    B = B->cp_b;
    HB = B->cp_h;
  } else {
    pt0->cp_ap = (yamop *)TRUSTFAILCODE;
  }
  return(TRUE);
}

Int
JumpToEnv(Term t) {
  yamop *pos = (yamop *)(PredDollarCatch->LastClause);
  CELL *env;
  choiceptr first_func = NULL, B0 = B;

  do {
    /* find the first choicepoint that may be a catch */
    while (B != NULL && B->cp_ap != pos) {
      /* we are already doing a catch */
      if (B->cp_ap == (yamop *)(PredHandleThrow->LastClause)) {
	P = (yamop *)FAILCODE;
	if (first_func != NULL) {
	  B = first_func;
	}
	return(FALSE);
      }
      if (B->cp_ap == NOCODE) {
	/* up to the C-code to deal with this! */
	B->cp_h = H;
	EX = t;
	return(FALSE);
      }
      B = B->cp_b;
    }
    /* uncaught throw */
    if (B == NULL) {
      B = B0;
      siglongjmp(RestartEnv,1);
    }
    /* is it a continuation? */
    env = B->cp_env;
    while (env > ENV)
      ENV = (CELL *)ENV[E_E];
    /* yes, we found it ! */
    if (env == ENV) break;
    /* oops, try next */
    B = B->cp_b;
  } while (TRUE);
  /* step one environment above */
  B->cp_cp = (yamop *)env[E_CP];
  B->cp_ap = (yamop *)(PredHandleThrow->LastClause);
  B->cp_env = (CELL *)env[E_E];
  /* cannot recover Heap because of copy term :-( */
  B->cp_h = H;
  /* I could backtrack here, but it is easier to leave the unwinding
     to the emulator */
  B->cp_a3 = t;
  P = (yamop *)FAILCODE;
  if (first_func != NULL) {
    B = first_func;
  }
#ifdef TABLING
  abolish_incomplete_subgoals(B);
#endif /* TABLING */
  return(FALSE);
}



/* This does very nasty stuff!!!!! */
static Int
p_jump_env(void) {
  return(JumpToEnv(Deref(ARG1)));
}


void 
InitExecFs(void)
{
  InitCPred("$execute", 1, p_execute, 0);
  InitCPred("$execute_in_mod", 2, p_execute_in_mod, 0);
  InitCPred("$execute_within", 4, p_execute_within, 0);
  InitCPred("$execute_within", 1, p_execute_within2, 0);
  InitCPred("$last_execute_within", 1, p_execute_within2, 0);
  InitCPred("$execute", 3, p_at_execute, 0);
  InitCPred("$call_with_args", 2, p_execute_0, 0);
  InitCPred("$call_with_args", 3, p_execute_1, 0);
  InitCPred("$call_with_args", 4, p_execute_2, 0);
  InitCPred("$call_with_args", 5, p_execute_3, 0);
  InitCPred("$call_with_args", 6, p_execute_4, 0);
  InitCPred("$call_with_args", 7, p_execute_5, 0);
  InitCPred("$call_with_args", 8, p_execute_6, 0);
  InitCPred("$call_with_args", 9, p_execute_7, 0);
  InitCPred("$call_with_args", 10, p_execute_8, 0);
  InitCPred("$call_with_args", 11, p_execute_9, 0);
  InitCPred("$call_with_args", 12, p_execute_10, 0);
#ifdef DEPTH_LIMIT
  InitCPred("$execute_under_depth_limit", 2, p_execute_depth_limit, 0);
#endif
  InitCPred("$execute0", 2, p_execute0, 0);
  InitCPred("$save_current_choice_point", 1, p_save_cp, 0);
  InitCPred("$pred_goal_expansion_on", 0, p_pred_goal_expansion_on, SafePredFlag);
  InitCPred("$restore_regs", 1, p_restore_regs, SafePredFlag);
  InitCPred("$restore_regs", 2, p_restore_regs2, SafePredFlag);
  InitCPred("$clean_ifcp", 1, p_clean_ifcp, SafePredFlag);
  InitCPred("$jump_env_and_store_ball", 1, p_jump_env, 0);
}

