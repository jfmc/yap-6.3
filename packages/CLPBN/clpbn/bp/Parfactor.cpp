
#include "Parfactor.h"
#include "Histogram.h"
#include "Indexer.h"
#include "Util.h"
#include "Horus.h"


Parfactor::Parfactor (
    const ProbFormulas& formulas,
    const Params& params, 
    const Tuples& tuples,
    unsigned distId)
{
  args_   = formulas;
  params_ = params;
  distId_ = distId;

  LogVars logVars;
  for (unsigned i = 0; i < args_.size(); i++) {
    ranges_.push_back (args_[i].range());
    const LogVars& lvs = args_[i].logVars();
    for (unsigned j = 0; j < lvs.size(); j++) {
      if (Util::contains (logVars, lvs[j]) == false) {
        logVars.push_back (lvs[j]);
      }
    }
  }
  constr_ = new ConstraintTree (logVars, tuples);
  assert (params_.size() == Util::expectedSize (ranges_));
}



Parfactor::Parfactor (const Parfactor* g, const Tuple& tuple)
{
  args_    = g->arguments();
  params_  = g->params();
  ranges_  = g->ranges();
  distId_  = g->distId();
  constr_  = new ConstraintTree (g->logVars(), {tuple});
  assert (params_.size() == Util::expectedSize (ranges_));
}



Parfactor::Parfactor (const Parfactor* g, ConstraintTree* constr)
{
  args_    = g->arguments();
  params_  = g->params();
  ranges_  = g->ranges();
  distId_  = g->distId();
  constr_  = constr;
  assert (params_.size() == Util::expectedSize (ranges_));
}



Parfactor::Parfactor (const Parfactor& g)
{
  args_    = g.arguments();
  params_  = g.params();
  ranges_  = g.ranges();
  distId_  = g.distId();
  constr_  = new ConstraintTree (*g.constr());
  assert (params_.size() == Util::expectedSize (ranges_));
}



Parfactor::~Parfactor (void)
{
  delete constr_;
}



LogVarSet
Parfactor::countedLogVars (void) const
{
  LogVarSet set;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].isCounting()) {
      set.insert (args_[i].countedLogVar());
    }
  }
  return set;
}



LogVarSet
Parfactor::uncountedLogVars (void) const
{
  return constr_->logVarSet() - countedLogVars();
}



LogVarSet
Parfactor::elimLogVars (void) const
{
  LogVarSet requiredToElim = constr_->logVarSet();
  requiredToElim -= constr_->singletons();
  requiredToElim -= countedLogVars();
  return requiredToElim;
}



LogVarSet
Parfactor::exclusiveLogVars (unsigned fIdx) const
{
  assert (fIdx < args_.size());
  LogVarSet remaining;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (i != fIdx) {
      remaining |= args_[i].logVarSet();
    }
  }
  return args_[fIdx].logVarSet() - remaining;
}



void
Parfactor::setConstraintTree (ConstraintTree* newTree)
{
  delete constr_;
  constr_ = newTree;
}



void
Parfactor::sumOut (unsigned fIdx)
{
  assert (fIdx < args_.size());
  assert (args_[fIdx].contains (elimLogVars()));

  if (args_[fIdx].isCounting()) {
    unsigned N = constr_->getConditionalCount (
        args_[fIdx].countedLogVar());
    unsigned R = args_[fIdx].range();
    vector<double> numAssigns = HistogramSet::getNumAssigns (N, R);
    StatesIndexer sindexer (ranges_, fIdx);
    while (sindexer.valid()) {
      unsigned h = sindexer[fIdx];
      if (Globals::logDomain) {
        params_[sindexer] += numAssigns[h];
      } else {
        params_[sindexer] *= numAssigns[h];
      }
      ++ sindexer;
    }
  }

  Params copy = params_;
  params_.clear();
  params_.resize (copy.size() / ranges_[fIdx], LogAware::addIdenty());
  MapIndexer indexer (ranges_, fIdx);
  if (Globals::logDomain) {
    for (unsigned i = 0; i < copy.size(); i++) {
      params_[indexer] = Util::logSum (params_[indexer], copy[i]);
      ++ indexer;
    }
  } else {
    for (unsigned i = 0; i < copy.size(); i++) {
      params_[indexer] += copy[i];
      ++ indexer;
    }
  }

  LogVarSet excl = exclusiveLogVars (fIdx);
  if (args_[fIdx].isCounting()) {
    // counting log vars were already raised on counting conversion
    LogAware::pow (params_, constr_->getConditionalCount (
        excl - args_[fIdx].countedLogVar()));
  } else {
    LogAware::pow (params_, constr_->getConditionalCount (excl));
  }
  constr_->remove (excl);

  args_.erase (args_.begin() + fIdx);
  ranges_.erase (ranges_.begin() + fIdx);
}



void
Parfactor::multiply (Parfactor& g)
{
  alignAndExponentiate (this, &g);
  TFactor<ProbFormula>::multiply (g);
  constr_->join (g.constr(), true);
  simplifyGrounds();
  assert (constr_->isCartesianProduct (countedLogVars()));
}



bool
Parfactor::canCountConvert (LogVar X)
{
  if (nrFormulas (X) != 1) {
    return false;
  }
  int fIdx = indexOfLogVar (X);
  if (args_[fIdx].isCounting()) {
    return false;
  }
  if (constr_->isCountNormalized (X) == false) {
    return false;
  }
  if (constr_->getConditionalCount (X) == 1) {
    return false;
  }
  if (constr_->isCartesianProduct (countedLogVars() | X) == false) {
    return false;
  }
  return true;
}



void
Parfactor::countConvert (LogVar X)
{
  int fIdx = indexOfLogVar (X);
  assert (constr_->isCountNormalized (X));
  assert (constr_->getConditionalCount (X) > 1);
  assert (canCountConvert (X));
 
  unsigned N = constr_->getConditionalCount (X);
  unsigned R = ranges_[fIdx];
  unsigned H = HistogramSet::nrHistograms (N, R);
  vector<Histogram> histograms = HistogramSet::getHistograms (N, R);

  StatesIndexer indexer (ranges_);
  vector<Params> sumout (params_.size() / R);
  unsigned count = 0;
  while (indexer.valid()) {
    sumout[count].reserve (R);
    for (unsigned r = 0; r < R; r++) {
      sumout[count].push_back (params_[indexer]);
      indexer.increment (fIdx);
    }
    count ++;
    indexer.reset (fIdx);
    indexer.incrementExcluding (fIdx);
  }

  params_.clear();
  params_.reserve (sumout.size() * H);

  ranges_[fIdx] = H;
  MapIndexer mapIndexer (ranges_, fIdx);
  while (mapIndexer.valid()) {
    double prod = LogAware::multIdenty();
    unsigned i = mapIndexer.mappedIndex();
    unsigned h = mapIndexer[fIdx];
    for (unsigned r = 0; r < R; r++) {
      if (Globals::logDomain) {
        prod += LogAware::pow (sumout[i][r], histograms[h][r]);
      } else {
        prod *= LogAware::pow (sumout[i][r], histograms[h][r]);
      }
    }
    params_.push_back (prod);
    ++ mapIndexer;
  }
  args_[fIdx].setCountedLogVar (X);
  simplifyCountingFormulas (fIdx);
}



void
Parfactor::expand (LogVar X, LogVar X_new1, LogVar X_new2)
{
  int fIdx = indexOfLogVar (X);
  assert (fIdx != -1);
  assert (args_[fIdx].isCounting());

  unsigned N1 = constr_->getConditionalCount (X_new1);
  unsigned N2 = constr_->getConditionalCount (X_new2);
  unsigned N  = N1 + N2;
  unsigned R  = args_[fIdx].range();
  unsigned H1 = HistogramSet::nrHistograms (N1, R);
  unsigned H2 = HistogramSet::nrHistograms (N2, R);

  vector<Histogram> histograms  = HistogramSet::getHistograms (N,  R);
  vector<Histogram> histograms1 = HistogramSet::getHistograms (N1, R);
  vector<Histogram> histograms2 = HistogramSet::getHistograms (N2, R);

  vector<unsigned> sumIndexes;
  sumIndexes.reserve (H1 * H2);
  for (unsigned i = 0; i < H1; i++) {
    for (unsigned j = 0; j < H2; j++) {
      Histogram hist = histograms1[i];
      std::transform (
          hist.begin(), hist.end(),
          histograms2[j].begin(),
          hist.begin(),
          plus<int>());
      sumIndexes.push_back (HistogramSet::findIndex (hist, histograms));
    }
  }

  expandPotential (fIdx, H1 * H2, sumIndexes);

  args_.insert (args_.begin() + fIdx + 1, args_[fIdx]);
  args_[fIdx].rename (X, X_new1);
  args_[fIdx + 1].rename (X, X_new2);
  if (H1 == 2) {
    args_[fIdx].clearCountedLogVar();
  }
  if (H2 == 2) {
    args_[fIdx + 1].clearCountedLogVar();
  }
  ranges_.insert (ranges_.begin() + fIdx + 1, H2);
  ranges_[fIdx] = H1;
}



void
Parfactor::fullExpand (LogVar X)
{
  int fIdx = indexOfLogVar (X);
  assert (fIdx != -1);
  assert (args_[fIdx].isCounting());

  unsigned N = constr_->getConditionalCount (X);
  unsigned R = args_[fIdx].range();
  vector<Histogram> originHists = HistogramSet::getHistograms (N, R);
  vector<Histogram> expandHists = HistogramSet::getHistograms (1, R);
  assert (ranges_[fIdx] == originHists.size());
  vector<unsigned> sumIndexes;
  sumIndexes.reserve (N * R);

  Ranges expandRanges (N, R);
  StatesIndexer indexer (expandRanges);
  while (indexer.valid()) {
    vector<unsigned> hist (R, 0);
    for (unsigned n = 0; n < N; n++) {
      std::transform (
          hist.begin(), hist.end(),
          expandHists[indexer[n]].begin(),
          hist.begin(),
          plus<int>());
    }
    sumIndexes.push_back (HistogramSet::findIndex (hist, originHists));
    ++ indexer;
  }
  
  expandPotential (fIdx, std::pow (R, N), sumIndexes);

  ProbFormula f = args_[fIdx];
  args_.erase (args_.begin() + fIdx);
  ranges_.erase (ranges_.begin() + fIdx);
  LogVars newLvs = constr_->expand (X);
  assert (newLvs.size() == N);
  for (unsigned i = 0 ; i < N; i++) {
    ProbFormula newFormula (f.functor(), f.logVars(), f.range());
    newFormula.rename (X, newLvs[i]);
    args_.insert (args_.begin() + fIdx + i, newFormula);
    ranges_.insert (ranges_.begin() + fIdx + i, R);
  }
}



void
Parfactor::reorderAccordingGrounds (const Grounds& grounds)
{
  ProbFormulas newFormulas;
  for (unsigned i = 0; i < grounds.size(); i++) {
    for (unsigned j = 0; j < args_.size(); j++) {
      if (grounds[i].functor() == args_[j].functor() && 
          grounds[i].arity()   == args_[j].arity()) {
        constr_->moveToTop (args_[j].logVars());
        if (constr_->containsTuple (grounds[i].args())) {
          newFormulas.push_back (args_[j]);
          break;
        }
      }
    }
    assert (newFormulas.size() == i + 1);
  }
  reorderArguments (newFormulas);
}



void
Parfactor::absorveEvidence (const ProbFormula& formula, unsigned evidence)
{
  int fIdx = indexOf (formula);
  assert (fIdx != -1);
  LogVarSet excl = exclusiveLogVars (fIdx);
  assert (args_[fIdx].isCounting() == false);
  assert (constr_->isCountNormalized (excl));
  LogAware::pow (params_, constr_->getConditionalCount (excl));
  TFactor<ProbFormula>::absorveEvidence (formula, evidence);
  constr_->remove (excl);
}



void
Parfactor::setNewGroups (void)
{
  for (unsigned i = 0; i < args_.size(); i++) {
    args_[i].setGroup (ProbFormula::getNewGroup());
  }
}



void
Parfactor::applySubstitution (const Substitution& theta)
{
  for (unsigned i = 0; i < args_.size(); i++) {
    LogVars& lvs = args_[i].logVars();
    for (unsigned j = 0; j < lvs.size(); j++) {
      lvs[j] = theta.newNameFor (lvs[j]);
    }
    if (args_[i].isCounting()) {
      LogVar clv = args_[i].countedLogVar();
      args_[i].setCountedLogVar (theta.newNameFor (clv));
    }
  }
  constr_->applySubstitution (theta);
}



int
Parfactor::findGroup (const Ground& ground) const
{
  int group = -1;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].functor() == ground.functor() && 
        args_[i].arity()   == ground.arity()) {
      constr_->moveToTop (args_[i].logVars());
      if (constr_->containsTuple (ground.args())) {
        group = args_[i].group();
        break;
      }
    }
  }
  return group;
}



bool
Parfactor::containsGround (const Ground& ground) const
{
  return findGroup (ground) != -1;
}



bool
Parfactor::containsGroup (unsigned group) const
{
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].group() == group) {
      return true;
    }
  }
  return false;
}



unsigned
Parfactor::nrFormulas (LogVar X) const
{
  unsigned count = 0;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].contains (X)) {
      count ++;
    }
  }
  return count;
}



int
Parfactor::indexOfLogVar (LogVar X) const
{
  int idx = -1;
  assert (nrFormulas (X) == 1);
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].contains (X)) {
      idx = i;
      break;
    }
  }
  return idx;
}



int
Parfactor::indexOfGroup (unsigned group) const
{
  int pos = -1;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].group() == group) {
      pos = i;
      break;
    }
  }
  return pos;
}



unsigned
Parfactor::nrFormulasWithGroup (unsigned group) const
{
  unsigned count = 0;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (args_[i].group() == group) {
      count ++;
    }
  }
  return count;
}



vector<unsigned>
Parfactor::getAllGroups (void) const
{
  vector<unsigned> groups (args_.size());
  for (unsigned i = 0; i < args_.size(); i++) {
    groups[i] = args_[i].group();
  }
  return groups;
}



string
Parfactor::getLabel (void) const
{
  stringstream ss;
  ss << "phi(" ;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (i != 0) ss << "," ;
    ss << args_[i];
  }
  ss << ")" ;
  ConstraintTree copy (*constr_);
  copy.moveToTop (copy.logVarSet().elements());
  ss << "|" << copy.tupleSet();
  return ss.str();
}



void
Parfactor::print (bool printParams) const
{
  cout << "Formulas:  " ;
  for (unsigned i = 0; i < args_.size(); i++) {
    if (i != 0) cout << ", " ;
    cout << args_[i];
  }
  cout << endl;
  if (args_[0].group() != Util::maxUnsigned()) {
    vector<string> groups;
    for (unsigned i = 0; i < args_.size(); i++) {
      groups.push_back (string ("g") + Util::toString (args_[i].group()));
    }
    cout << "Groups:    " << groups  << endl;
  }
  cout << "LogVars:   " << constr_->logVarSet()  << endl;
  cout << "Ranges:    " << ranges_ << endl;
  if (printParams == false) {
    cout << "Params:    " ;
    if (params_.size() <= 32) {
      cout.precision(10);
      cout << params_ << endl;
    } else {
      cout << "|" << params_.size() << "|" << endl;
    }
  }
  ConstraintTree copy (*constr_);
  copy.moveToTop (copy.logVarSet().elements());
  cout << "Tuples:    " << copy.tupleSet() << endl;
  if (printParams) {
    printParameters();
  }
}



void
Parfactor::printParameters (void) const
{
  vector<string> jointStrings;
  StatesIndexer indexer (ranges_);
  while (indexer.valid()) {
    stringstream ss;
    for (unsigned i = 0; i < args_.size(); i++) {
      if (i != 0) ss << ", " ;
      if (args_[i].isCounting()) {
        unsigned N = constr_->getConditionalCount (
            args_[i].countedLogVar());
        HistogramSet hs (N, args_[i].range());
        unsigned c = 0;
        while (c < indexer[i]) {
          hs.nextHistogram();
          c ++;
        }
        ss << hs;
      } else {
        ss << indexer[i];
      }
    }
    jointStrings.push_back (ss.str());
    ++ indexer;
  }
  for (unsigned i = 0; i < params_.size(); i++) {
    cout << "f(" << jointStrings[i] << ")" ;
    cout << " = " << params_[i] << endl;
  }
}



void
Parfactor::printProjections (void) const
{
  ConstraintTree copy (*constr_);

  LogVarSet Xs = copy.logVarSet();
  for (unsigned i = 0; i < Xs.size(); i++) {
    cout << "-> projection of " << Xs[i] << ": " ;
    cout << copy.tupleSet ({Xs[i]}) << endl;
  }
}



void
Parfactor::expandPotential (
    int fIdx,
    unsigned newRange,
    const vector<unsigned>& sumIndexes)
{
  ullong newSize = (params_.size() / ranges_[fIdx]) * newRange;
  if (newSize > params_.max_size()) {
    cerr << "error: an overflow occurred when performing expansion" ;
    cerr << endl;
    abort();
  }

  Params copy = params_;
  params_.clear();
  params_.reserve (newSize);

  unsigned prod = 1;
  vector<unsigned> offsets_ (ranges_.size());
  for (int i = ranges_.size() - 1; i >= 0; i--) {
    offsets_[i] = prod;
    prod *= ranges_[i];
  }

  unsigned index = 0;
  ranges_[fIdx] = newRange;
  vector<unsigned> indices (ranges_.size(), 0);
  for (unsigned k = 0; k < newSize; k++) {
    if (index >= copy.size()) {
      abort();
    }
    assert (index < copy.size());
    params_.push_back (copy[index]);
    for (int i = ranges_.size() - 1; i >= 0; i--) {
      indices[i] ++;
      if (i == fIdx) {
        if (indices[i] != ranges_[i]) {
          int diff = sumIndexes[indices[i]] - sumIndexes[indices[i] - 1];
          index += diff * offsets_[i];
          break;
        } else {
          // last index contains the old range minus 1
          index -= sumIndexes.back() * offsets_[i];
          indices[i] = 0;
        }
      } else {
        if (indices[i] != ranges_[i]) {
          index += offsets_[i];
          break;
        } else {
          index -= (ranges_[i] - 1) * offsets_[i];
          indices[i] = 0;
        }
      }
    }
  }
}



void
Parfactor::simplifyCountingFormulas (int fIdx)
{
  // check if we can simplify the parfactor
  for (unsigned i = 0; i < args_.size(); i++) {
    if ((int)i != fIdx &&
        args_[i].isCounting() &&
        args_[i].group() == args_[fIdx].group()) {
      // if they only differ in the name of the counting log var
      if ((args_[i].logVarSet() - args_[i].countedLogVar()) ==
          (args_[fIdx].logVarSet()) - args_[fIdx].countedLogVar() &&
           ranges_[i] == ranges_[fIdx]) {
        simplifyParfactor (fIdx, i);
        break;
      }
    }
  }
}



void
Parfactor::simplifyGrounds (void)
{
  LogVarSet singletons = constr_->singletons();
  for (int i = 0; i < (int)args_.size() - 1; i++) {
    for (unsigned j = i + 1; j < args_.size(); j++) {
      if (args_[i].group() == args_[j].group() &&
          singletons.contains (args_[i].logVarSet()) &&
          singletons.contains (args_[j].logVarSet())) {
        simplifyParfactor (i, j);
        i --;
        break;
      }
    }
  }
}



bool
Parfactor::canMultiply (Parfactor* g1, Parfactor* g2)
{
  std::pair<LogVars, LogVars> res = getAlignLogVars (g1, g2);
  LogVarSet Xs_1 (res.first);
  LogVarSet Xs_2 (res.second);
  LogVarSet Y_1 = g1->logVarSet() - Xs_1;
  LogVarSet Y_2 = g2->logVarSet() - Xs_2;
  Y_1 -= g1->countedLogVars();
  Y_2 -= g2->countedLogVars();
  return g1->constr()->isCountNormalized (Y_1) &&
         g2->constr()->isCountNormalized (Y_2);
}



void
Parfactor::simplifyParfactor (unsigned fIdx1, unsigned fIdx2)
{
  Params copy = params_;
  params_.clear();
  StatesIndexer indexer (ranges_);
  while (indexer.valid()) {
    if (indexer[fIdx1] == indexer[fIdx2]) {
      params_.push_back (copy[indexer]);
    }     
    ++ indexer;
  }
  for (unsigned i = 0; i < args_[fIdx2].logVars().size(); i++) {
    if (nrFormulas (args_[fIdx2].logVars()[i]) == 1) {
      constr_->remove ({ args_[fIdx2].logVars()[i] });
    }
  }
  args_.erase (args_.begin() + fIdx2);
  ranges_.erase (ranges_.begin() + fIdx2);
}



std::pair<LogVars, LogVars>
Parfactor::getAlignLogVars (Parfactor* g1, Parfactor* g2)
{
  g1->simplifyGrounds();
  g2->simplifyGrounds();
  LogVars Xs_1, Xs_2;
  TinySet<unsigned> matchedI;
  TinySet<unsigned> matchedJ;
  ProbFormulas& formulas1 = g1->arguments();
  ProbFormulas& formulas2 = g2->arguments(); 
  for (unsigned i = 0; i < formulas1.size(); i++) {
    for (unsigned j = 0; j < formulas2.size(); j++) {
      if (formulas1[i].group() == formulas2[j].group() &&
          g1->range (i) == g2->range (j) &&
          matchedI.contains (i) == false &&
          matchedJ.contains (j) == false) {
        Util::addToVector (Xs_1, formulas1[i].logVars());
        Util::addToVector (Xs_2, formulas2[j].logVars());
        matchedI.insert (i);
        matchedJ.insert (j);
      }
    }
  }
  return make_pair (Xs_1, Xs_2);
}



void
Parfactor::alignAndExponentiate (Parfactor* g1, Parfactor* g2)
{
  alignLogicalVars (g1, g2);
  LogVarSet comm = g1->logVarSet() & g2->logVarSet();
  LogVarSet Y_1 = g1->logVarSet() - comm;
  LogVarSet Y_2 = g2->logVarSet() - comm;
  Y_1 -= g1->countedLogVars();
  Y_2 -= g2->countedLogVars();
  assert (g1->constr()->isCountNormalized (Y_1));
  assert (g2->constr()->isCountNormalized (Y_2));
  unsigned condCount1 = g1->constr()->getConditionalCount (Y_1);
  unsigned condCount2 = g2->constr()->getConditionalCount (Y_2);
  LogAware::pow (g1->params(), 1.0 / condCount2);
  LogAware::pow (g2->params(), 1.0 / condCount1);
}



void
Parfactor::alignLogicalVars (Parfactor* g1, Parfactor* g2)
{
  std::pair<LogVars, LogVars> res = getAlignLogVars (g1, g2);
  const LogVars& alignLvs1 = res.first;
  const LogVars& alignLvs2 = res.second;
  // cout << "ALIGNING :::::::::::::::::" << endl;
  // g1->print();
  // cout << "AND" << endl;
  // g2->print();
  // cout << "-> align lvs1 = " << alignLvs1 << endl;
  // cout << "-> align lvs2 = " << alignLvs2 << endl;
  LogVar freeLogVar (0);
  Substitution theta1, theta2;
  for (unsigned i = 0; i < alignLvs1.size(); i++) {
    bool b1 = theta1.containsReplacementFor (alignLvs1[i]); 
    bool b2 = theta2.containsReplacementFor (alignLvs2[i]);
    if (b1 == false && b2 == false) {
      theta1.add (alignLvs1[i], freeLogVar);
      theta2.add (alignLvs2[i], freeLogVar);
      ++ freeLogVar;
    } else if (b1 == false && b2) {
      theta1.add (alignLvs1[i], theta2.newNameFor (alignLvs2[i]));
    } else if (b1 && b2 == false) {
      theta2.add (alignLvs2[i], theta1.newNameFor (alignLvs1[i]));
    }
  }

  const LogVarSet& allLvs1 = g1->logVarSet();
  for (unsigned i = 0; i < allLvs1.size(); i++) {
    if (theta1.containsReplacementFor (allLvs1[i]) == false) {
      theta1.add (allLvs1[i], freeLogVar);
      ++ freeLogVar;
    }
  }
  const LogVarSet& allLvs2 = g2->logVarSet();
  for (unsigned i = 0; i < allLvs2.size(); i++) {
    if (theta2.containsReplacementFor (allLvs2[i]) == false) {
      theta2.add (allLvs2[i], freeLogVar);
      ++ freeLogVar;
    }
  }

  // handle this type of situation:
  // g1 = p(X), q(X) ;  X    in {(p1),(p2)} 
  // g2 = p(X), q(Y) ; (X,Y) in {(p1,p2),(p2,p1)}
  LogVars discardedLvs1 = theta1.getDiscardedLogVars();
  for (unsigned i = 0; i < discardedLvs1.size(); i++) {
    if (g1->constr()->isSingleton (discardedLvs1[i]) && 
        g1->nrFormulas (discardedLvs1[i]) == 1) {
      g1->constr()->remove (discardedLvs1[i]);
    } else {
      LogVar X_new = ++ g1->constr()->logVarSet().back();
      theta1.rename (discardedLvs1[i], X_new);
    }
  }
  LogVars discardedLvs2 = theta2.getDiscardedLogVars();
  for (unsigned i = 0; i < discardedLvs2.size(); i++) {
    if (g2->constr()->isSingleton (discardedLvs2[i]) &&
        g2->nrFormulas (discardedLvs2[i]) == 1) {
      g2->constr()->remove (discardedLvs2[i]);
    } else {
      LogVar X_new = ++ g2->constr()->logVarSet().back();
      theta2.rename (discardedLvs2[i], X_new);
    }
  }

  // cout << "theta1: " << theta1 << endl;
  // cout << "theta2: " << theta2 << endl;
  g1->applySubstitution (theta1);
  g2->applySubstitution (theta2);
}

