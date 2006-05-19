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
* File:		agc.c							 *
* Last rev:								 *
* mods:									 *
* comments:	reclaim unused atoms and functors			 *
*									 *
*************************************************************************/
#ifdef SCCS
static char     SccsId[] = "@(#)agc.c	1.3 3/15/90";
#endif


#include "absmi.h"
#include "alloc.h"
#include "yapio.h"
#include "attvar.h"

#ifdef DEBUG
/* #define DEBUG_RESTORE1 1 */
/* #define DEBUG_RESTORE2 1 */
#define DEBUG_RESTORE3 1
#define errout Yap_stderr
#endif

STATIC_PROTO(void  RestoreEntries, (PropEntry *));
STATIC_PROTO(void  CleanCode, (PredEntry *));

static int agc_calls;

static unsigned long int agc_collected;

static Int tot_agc_time = 0; /* total time spent in GC */

static Int tot_agc_recovered = 0; /* number of heap objects in all garbage collections */

#define AtomMarkedBit 1

static inline void
MarkAtomEntry(AtomEntry *ae)
{
  CELL c = (CELL)(ae->NextOfAE);
  c |= AtomMarkedBit;
  ae->NextOfAE = (Atom)c;
}

static inline int
AtomResetMark(AtomEntry *ae)
{
  CELL c = (CELL)(ae->NextOfAE);
  if (c & AtomMarkedBit) {
    c &= ~AtomMarkedBit;
    ae->NextOfAE = (Atom)c;
    return (TRUE);
  }
  return (FALSE);
}

static inline Atom
CleanAtomMarkedBit(Atom a)
{
  CELL c = (CELL)a;
  c &= ~AtomMarkedBit;
  return((Atom)c);
}

static inline Functor
FuncAdjust(Functor f)
{
  if (!IsExtensionFunctor(f)) {  
    AtomEntry *ae = RepAtom(NameOfFunctor(f));
    MarkAtomEntry(ae);
  }
  return(f);
}


static inline Term
AtomTermAdjust(Term t)
{
  AtomEntry *ae = RepAtom(AtomOfTerm(t));
  MarkAtomEntry(ae);
  return(t);  
}

static inline Atom
AtomAdjust(Atom a)
{
  AtomEntry *ae;
  if (a == NIL) return(a);
  ae = RepAtom(a);
  MarkAtomEntry(ae);
  return(a);
}

#define IsOldCode(P) FALSE
#define IsOldCodeCellPtr(P) FALSE
#define IsOldDelay(P) FALSE
#define IsOldDelayPtr(P) FALSE
#define IsOldLocalInTR(P) FALSE
#define IsOldLocalInTRPtr(P) FALSE
#define IsOldGlobal(P) FALSE
#define IsOldGlobalPtr(P) FALSE
#define IsOldTrail(P) FALSE
#define IsOldTrailPtr(P) FALSE

#define CharP(X) ((char *)(X))

#define AddrAdjust(P) (P)
#define AtomEntryAdjust(P) (P)
#define BlobTermAdjust(P) (P)
#define CellPtoHeapAdjust(P) (P)
#define CellPtoHeapCellAdjust(P) (P)
#define CellPtoTRAdjust(P) (P)
#define CodeAddrAdjust(P) (P)
#define ConsultObjAdjust(P) (P)
#define DelayAddrAdjust(P) (P)
#define DBRefAdjust(P) (P)
#define DBRefPAdjust(P) (P)
#define DBTermAdjust(P) (P)
#define LUIndexAdjust(P) (P)
#define SIndexAdjust(P) (P)
#define LocalAddrAdjust(P) (P)
#define GlobalAddrAdjust(P) (P)
#define PtoLUCAdjust(P) (P)
#define PtoStCAdjust(P) (P)
#define PtoArrayEAdjust(P) (P)
#define PtoArraySAdjust(P) (P)
#define PtoDelayAdjust(P) (P)
#define PtoGloAdjust(P) (P)
#define PtoLocAdjust(P) (P)
#define PtoHeapCellAdjust(P) (P)
#define PtoOpAdjust(P) (P)
#define PtoPredAdjust(P) (P)
#define PropAdjust(P) (P)
#define TrailAddrAdjust(P) (P)
#define XAdjust(P) (P)
#define YAdjust(P) (P)

static void
recompute_mask(DBRef dbr)
{
  return;
}

static void 
rehash(CELL *oldcode, int NOfE, int KindOfEntries)
{
}

#include "rheap.h"

/*
 * This is the really tough part, to restore the whole of the heap 
 */
static void 
mark_atoms(void)
{
  AtomHashEntry *HashPtr = HashChain;
  register int    i;
  Atom atm;
  AtomEntry      *at;

  restore_codes();
  for (i = 0; i < AtomHashTableSize; ++i) {
    atm = HashPtr->Entry;
    if (atm) {
      at =  RepAtom(atm);
      do {
#ifdef DEBUG_RESTORE1			/* useful during debug */
	fprintf(errout, "Restoring %s\n", at->StrOfAE);
#endif
	RestoreEntries(RepProp(at->PropsOfAE));
	atm = at->NextOfAE;
	at = RepAtom(CleanAtomMarkedBit(atm));
      } while (!EndOfPAEntr(at));
    }
    HashPtr++;
  }

  atm = INVISIBLECHAIN.Entry;
  at = RepAtom(atm);
  if (EndOfPAEntr(at)) {
    return;
  }
  do {
#ifdef DEBUG_RESTORE1		/* useful during debug */
    fprintf(errout, "Restoring %s\n", at->StrOfAE);
#endif
    RestoreEntries(RepProp(at->PropsOfAE));
    atm = at->NextOfAE;
    at = RepAtom(CleanAtomMarkedBit(atm));
  } while (!EndOfPAEntr(at));
}

static void
mark_trail(void)
{
  register CELL *pt;

  pt = (CELL *)TR;
  /* moving the trail is simple */
  while (pt != (CELL *)Yap_TrailBase) {
    register CELL reg = pt[-1];
    pt--;
    if (!IsVarTerm(reg)) {
      if (IsAtomTerm(reg)) {
	MarkAtomEntry(RepAtom(AtomOfTerm(reg)));
      }
    }
  }
}

static void
mark_local(void)
{
  register CELL   *pt;

  /* Adjusting the local */
  pt = LCL0;
  /* moving the trail is simple */
  while (pt > ASP) {
    CELL reg = *--pt;

    if (!IsVarTerm(reg)) {
      if (IsAtomTerm(reg)) {
	MarkAtomEntry(RepAtom(AtomOfTerm(reg)));
      }
    }
  }
}

static CELL *
mark_global_cell(CELL *pt)
{   
  CELL reg = *pt;

  if (IsVarTerm(reg)) {
    /* skip bitmaps */
    switch(reg) {
    case (CELL)FunctorDouble:
#if SIZEOF_DOUBLE == 2*SIZEOF_LONG_INT
      return pt + 4;
#else
      return pt + 3;
#endif
    case (CELL)FunctorIntArray:
      return pt + (4+pt[1]);
    case (CELL)FunctorDoubleArray:
      return pt + (4+((SIZEOF_DOUBLE -SIZEOF_LONG_INT) + pt[1]*SIZEOF_DOUBLE)/SIZEOF_LONG_INT);
#if USE_GMP
    case (CELL)FunctorBigInt:
      {
	Int sz = 1+
	  sizeof(MP_INT)+
	  (((MP_INT *)(pt+1))->_mp_alloc*sizeof(mp_limb_t));
	return pt + sz+1;
      }
#endif
    case (CELL)FunctorLongInt:
      return pt += 3;
      break;
    }
  } else if (IsAtomTerm(reg)) {
    MarkAtomEntry(RepAtom(AtomOfTerm(reg)));
    return pt+1;
  }
  return pt+1;
}

static void
mark_global(void)
{
  CELL *pt;

  /*
   * to clean the global now that functors are just variables pointing to
   * the code 
   */
#if COROUTINING
  CELL *ptf = (CELL *)DelayTop();

  pt = (CELL *)Yap_GlobalBase;
  while (pt < ptf) {
    pt = mark_global_cell(pt);
  }
#endif
  pt = H0;
  while (pt < H) {
    pt = mark_global_cell(pt);
  }
}

static void
mark_stacks(void)
{
  mark_trail();
  mark_local();
  mark_global();
}

/*
 * This is the really tough part, to restore the whole of the heap 
 */
static void 
clean_atoms(void)
{
  AtomHashEntry *HashPtr = HashChain;
  register int    i;
  Atom atm;
  Atom *patm;
  AtomEntry  *at;

  for (i = 0; i < AtomHashTableSize; ++i) {
    atm = HashPtr->Entry;
    patm = &(HashPtr->Entry);
    while (atm != NIL) {
      at =  RepAtom(CleanAtomMarkedBit(atm));
      if (AtomResetMark(at) || (AGCHook != NULL && !AGCHook(atm))) {
	patm = &(at->NextOfAE);
	atm = at->NextOfAE;
	NOfAtoms--;
      } else {
#ifdef DEBUG_RESTORE3
	fprintf(stderr, "Purged %p:%s\n", at, at->StrOfAE);
#endif
	*patm = at->NextOfAE;
	atm = at->NextOfAE;
	agc_collected += sizeof(AtomEntry)+strlen(at->StrOfAE);
	Yap_FreeCodeSpace((char *)at);
      }
    }
    HashPtr++;
  }
  patm = &(INVISIBLECHAIN.Entry);
  atm = INVISIBLECHAIN.Entry;
  while (atm != NIL) {
    at =  RepAtom(CleanAtomMarkedBit(atm));
    if (AtomResetMark(at) || (AGCHook != NULL && !AGCHook(atm))) {
      patm = &(atm->NextOfAE);
      NOfAtoms--;
      atm = at->NextOfAE;
    } else {
#ifdef DEBUG_RESTORE1
      fprintf(stderr, "Purged %s\n", at->StrOfAE);
#endif
      *patm = at->NextOfAE;
      atm = at->NextOfAE;
      agc_collected += sizeof(AtomEntry) + strlen(at->StrOfAE);
      Yap_FreeCodeSpace((char *)at);
    }
  }
}

static void
atom_gc(void)
{
  int		gc_verbose = Yap_is_gc_verbose();
  int           gc_trace = 0;
  

  UInt		time_start, agc_time;
  if (Yap_GetValue(AtomGcTrace) != TermNil)
    gc_trace = 1;
  agc_calls++;
  agc_collected = 0;
  if (gc_trace) {
    fprintf(Yap_stderr, "%% agc:\n");
  } else if (gc_verbose) {
    fprintf(Yap_stderr, "%%   Start of atom garbage collection %d:\n", agc_calls);
  }
  time_start = Yap_cputime();
  /* get the number of active registers */
  YAPEnterCriticalSection();
  mark_stacks();
  mark_atoms();
  clean_atoms();
  YAPLeaveCriticalSection();
  agc_time = Yap_cputime()-time_start;
  tot_agc_time += agc_time;
  tot_agc_recovered += agc_collected;
  if (gc_verbose) {
    fprintf(Yap_stderr, "%%   Collected %ld bytes.\n", agc_collected);
    fprintf(Yap_stderr, "%%   GC %d took %g sec, total of %g sec doing GC so far.\n", agc_calls, (double)agc_time/1000, (double)tot_agc_time/1000);
  }
}

void
Yap_atom_gc(void)
{
  atom_gc();
}

static Int
p_atom_gc(void)
{
  return TRUE;
#ifndef FIXED_STACKS
  atom_gc();
#endif  /* FIXED_STACKS */
  return TRUE;
}

static Int
p_inform_agc(void)
{
  Term tn = MkIntegerTerm(tot_agc_time);
  Term tt = MkIntegerTerm(agc_calls);
  Term ts = MkIntegerTerm(tot_agc_recovered);

  return(Yap_unify(tn, ARG2) && Yap_unify(tt, ARG1) && Yap_unify(ts, ARG3));

}

void 
Yap_init_agc(void)
{
  Yap_InitCPred("$atom_gc", 0, p_atom_gc, HiddenPredFlag);
  Yap_InitCPred("$inform_agc", 3, p_inform_agc, HiddenPredFlag);
}
