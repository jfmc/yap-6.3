#include "ProbFormula.h"


PrvGroup ProbFormula::freeGroup_ = 0;



bool
ProbFormula::sameSkeletonAs (const ProbFormula& f) const
{
  return functor_ == f.functor() && logVars_.size() == f.arity();
}



bool
ProbFormula::contains (LogVar lv) const
{
  return Util::contains (logVars_, lv);
}



bool
ProbFormula::contains (LogVarSet s) const
{
  return LogVarSet (logVars_).contains (s);
}



size_t
ProbFormula::indexOf (LogVar X) const
{
  return Util::indexOf (logVars_, X);
}



bool
ProbFormula::isAtom (void) const
{
  return logVars_.empty();
}



bool
ProbFormula::isCounting (void) const
{
  return countedLogVar_.valid();
}



LogVar
ProbFormula::countedLogVar (void) const
{
  assert (isCounting());
  return countedLogVar_;
}



void
ProbFormula::setCountedLogVar (LogVar lv)
{
  countedLogVar_ = lv;
}



void
ProbFormula::clearCountedLogVar (void)
{
  countedLogVar_ = LogVar();
}



void
ProbFormula::rename (LogVar oldName, LogVar newName)
{
  for (size_t i = 0; i < logVars_.size(); i++) {
    if (logVars_[i] == oldName) {
      logVars_[i] = newName;
    }
  }
  if (isCounting() && countedLogVar_ == oldName) {
    countedLogVar_ = newName;
  }
}



bool operator== (const ProbFormula& f1, const ProbFormula& f2)
{
  return f1.group_   == f2.group_ &&
         f1.logVars_ == f2.logVars_;
}



std::ostream& operator<< (ostream &os, const ProbFormula& f)
{
  os << f.functor_;
  if (f.isAtom() == false) {
    os << "(" ;
    for (size_t i = 0; i < f.logVars_.size(); i++) {
     if (i != 0) os << ",";
      if (f.isCounting() && f.logVars_[i] == f.countedLogVar_) {
        os << "#" ;
      }
      os << f.logVars_[i];
    }
    os << ")" ;
  }
  os << "::" << f.range_;
  return os;
}



PrvGroup
ProbFormula::getNewGroup (void)
{
  freeGroup_ ++;
  assert (freeGroup_ != std::numeric_limits<PrvGroup>::max());
  return freeGroup_;
}



ObservedFormula::ObservedFormula (Symbol f, unsigned a, unsigned ev)
    : functor_(f), arity_(a), evidence_(ev), constr_(a)
{

}



ObservedFormula::ObservedFormula (Symbol f, unsigned ev, const Tuple& tuple)
    : functor_(f), arity_(tuple.size()), evidence_(ev), constr_(arity_)
{
  constr_.addTuple (tuple);
}



ostream& operator<< (ostream &os, const ObservedFormula& of)
{
  os << of.functor_ << "/" << of.arity_;
  os << "|" << of.constr_.tupleSet();
  os << " [evidence=" << of.evidence_ << "]";
  return os;
}

