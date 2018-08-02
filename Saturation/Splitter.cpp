
/*
 * File Splitter.cpp.
 *
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 *
 * In summary, you are allowed to use Vampire for non-commercial
 * purposes but not allowed to distribute, modify, copy, create derivatives,
 * or use in competitions. 
 * For other uses of Vampire please contact developers for a different
 * licence, which we will make an effort to provide. 
 */
/**
 * @file Splitter.cpp
 * Implements class Splitter.
 */

#include "Splitter.hpp"

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/DHSet.hpp"
#include "Lib/Environment.hpp"
#include "Lib/IntUnionFind.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/SharedSet.hpp"
#include "Lib/TimeCounter.hpp"

#include "Kernel/Signature.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/RCClauseStack.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/MainLoop.hpp"

#include "Shell/Options.hpp"
#include "Shell/Refutation.hpp"
#include "Shell/Statistics.hpp"

#include "SAT/Preprocess.hpp"
#include "SAT/SATInference.hpp"
#include "SAT/TWLSolver.hpp"
#include "SAT/MinimizingSolver.hpp"
#include "SAT/BufferedSolver.hpp"
#include "SAT/FallbackSolverWrapper.hpp"
#include "SAT/MinisatInterfacing.hpp"
#include "SAT/Z3Interfacing.hpp"

#include "DP/ShortConflictMetaDP.hpp"

#include "SaturationAlgorithm.hpp"

namespace Saturation
{

using namespace Lib;
using namespace Kernel;


/////////////////////////////
// SplittingBranchSelector
//

void SplittingBranchSelector::init()
{
  CALL("SplittingBranchSelector::init");

  _eagerRemoval = _parent.getOptions().splittingEagerRemoval();
  _literalPolarityAdvice = _parent.getOptions().splittingLiteralPolarityAdvice();

  switch(_parent.getOptions().satSolver()){
    case Options::SatSolver::VAMPIRE:  
      _solver = new TWLSolver(_parent.getOptions(), true);
      break;
    case Options::SatSolver::MINISAT:
      _solver = new MinisatInterfacing(_parent.getOptions(),true);
      break;      
#if VZ3
    case Options::SatSolver::Z3:
      { BYPASSING_ALLOCATOR
        _solver = new Z3Interfacing(_parent.getOptions(),_parent.satNaming());
        if(_parent.getOptions().satFallbackForSMT()){
          // TODO make fallback minimizing?
          SATSolver* fallback = new MinisatInterfacing(_parent.getOptions(),true);
          _solver = new FallbackSolverWrapper(_solver.release(),fallback);
        } 
      }
      break;
#endif
    default:
      ASSERTION_VIOLATION_REP(_parent.getOptions().satSolver());
  }

  if (_parent.getOptions().splittingBufferedSolver()) {
    _solver = new BufferedSolver(_solver.release());
  }

  switch(_parent.getOptions().splittingMinimizeModel()){
    case Options::SplittingMinimizeModel::OFF:
      // Do nothing - we don't want to minimise the model
      break;
    case Options::SplittingMinimizeModel::ALL:
    case Options::SplittingMinimizeModel::SCO:
      _solver = new MinimizingSolver(_solver.release());
      break;
    default:
      ASSERTION_VIOLATION_REP(_parent.getOptions().splittingMinimizeModel());
  }
  _minSCO = _parent.getOptions().splittingMinimizeModel() == Options::SplittingMinimizeModel::SCO;

  if(_parent.getOptions().splittingCongruenceClosure() != Options::SplittingCongruenceClosure::OFF) {
    _dp = new DP::SimpleCongruenceClosure(&_parent.getOrdering());
    if (_parent.getOptions().ccUnsatCores() == Options::CCUnsatCores::SMALL_ONES) {
      _dp = new ShortConflictMetaDP(_dp.release(), _parent.satNaming(), *_solver);
    }
    _ccMultipleCores = (_parent.getOptions().ccUnsatCores() != Options::CCUnsatCores::FIRST);

    _ccModel = (_parent.getOptions().splittingCongruenceClosure() == Options::SplittingCongruenceClosure::MODEL);
    if (_ccModel) {
      _dpModel = new DP::SimpleCongruenceClosure(&_parent.getOrdering());
    }
  }
}

void SplittingBranchSelector::updateVarCnt()
{
  CALL("SplittingBranchSelector::updateVarCnt");

  unsigned satVarCnt = _parent.maxSatVar();
  unsigned splitLvlCnt = _parent.splitLevelCnt();

  // index by var, but ignore slot 0
  _selected.expand(splitLvlCnt+1);
  _trueInCCModel.expand(satVarCnt+1);

  // solver may be doing the same, but only internally
  _solver->ensureVarCount(satVarCnt);
}

/**
 * The solver should consider making @b lit false by default.
 */
void SplittingBranchSelector::considerPolarityAdvice(SATLiteral lit)
{
  CALL("SplittingBranchSelector::considerPolarityAdvice");

  switch (_literalPolarityAdvice) {
    case Options::SplittingLiteralPolarityAdvice::FALSE:
      _solver->suggestPolarity(lit.var(),lit.oppositePolarity());
    break;
    case Options::SplittingLiteralPolarityAdvice::TRUE:
      _solver->suggestPolarity(lit.var(),lit.polarity());
    break;
    case Options::SplittingLiteralPolarityAdvice::NONE:
      // do nothing
    break;
  }
}

static Color colorFromPossiblyDeepFOConversion(SATClause* scl,Unit*& u)
{
  /* all the clauses added to AVATAR are FO_CONVERSIONs except when there is a duplicate literal
   and Preprocess::removeDuplicateLiterals creates an extra inference with a single premise ``in between''.*/
  if (scl->inference()->getType() != SATInference::FO_CONVERSION) {
    ASS_EQ(scl->inference()->getType(),SATInference::PROP_INF);
    PropInference* inf = static_cast<PropInference*>(scl->inference());
    SATClauseList* premises = inf->getPremises();
    ASS_EQ(SATClauseList::length(premises),1);
    scl = premises->head();
  }

  SATInference* inf = scl->inference();

  ASS_EQ(inf->getType(),SATInference::FO_CONVERSION);
  u = static_cast<FOConversionInference*>(inf)->getOrigin();
  Inference* i = u->inference();
  Inference::Iterator it = i->iterator();
  ASS(i->hasNext(it));
  Unit* u1 = i->next(it);
  ASS(u1->isClause());
  Clause* cl = u1->asClause();
  return cl->color();
}

void SplittingBranchSelector::handleSatRefutation()
{
  CALL("SplittingBranchSelector::handleSatRefutation");

  SATClause* satRefutation = _solver->getRefutation();
  SATClauseList* satPremises = _solver->getRefutationPremiseList();

  if (!env.colorUsed) { // color oblivious, simple approach
    UnitList* prems = SATInference::getFOPremises(satRefutation);

    UnitList::Iterator pit(prems);
    unsigned maxInd = 0;
    while(pit.hasNext()){
      Unit* u = pit.next();
      Inference* inf = u->inference();
      Inference::Iterator iit = inf->iterator();
      while(inf->hasNext(iit)) {
        Unit* premUnit = inf->next(iit);
        if(premUnit->isClause()){
          unsigned ind = static_cast<Clause*>(premUnit)->inductionDepth();
          if(ind>maxInd){ maxInd=ind; }
        } 
      }     
    }
    env.statistics->maxInductionDepth=maxInd;

    Inference* foInf = satPremises ? // does our SAT solver support postponed minimization?
        new InferenceFromSatRefutation(Inference::AVATAR_REFUTATION, prems, satPremises) :
        new InferenceMany(Inference::AVATAR_REFUTATION, prems);

    Clause* foRef = Clause::fromIterator(LiteralIterator::getEmpty(), Unit::CONJECTURE, foInf);
    throw MainLoop::RefutationFoundException(foRef);
  } else { // we must produce a well colored proof

    // collect actually used SAT premises
    SATClauseStack actualSatPremises;

    if (satPremises) { // does our SAT solver support postponed minimization?
      SATLiteralStack dummy;
      SATClauseList* minimizedSatPremises = MinisatInterfacing::minimizePremiseList(satPremises,dummy);

      actualSatPremises.loadFromIterator(SATClauseList::DestructiveIterator(minimizedSatPremises));
    } else {
      SATInference::collectPropAxioms(satRefutation,actualSatPremises);
    }

    // decide which side is "bigger" and should go "first"
    int colorCnts[3] = {0,0,0};
    SATClauseStack::Iterator it1(actualSatPremises);
    while (it1.hasNext()) {
      SATClause* scl = it1.next();
      // cout << "SAT: " << scl->toString() << endl;

      Unit* dummy;
      Color c = colorFromPossiblyDeepFOConversion(scl,dummy);

      ASS_L(c,COLOR_INVALID);
      colorCnts[c]++;
    }

    //cout << colorCnts[0] << " " << colorCnts[1] <<  " " << colorCnts[2] << endl;
    Color sndCol = COLOR_RIGHT;
    if (colorCnts[COLOR_LEFT] < colorCnts[COLOR_RIGHT]) {
      sndCol = COLOR_LEFT;
    }

    // split into first and second
    SATClauseStack first;
    UnitList* first_prems = UnitList::empty();
    SATClauseStack second;
    UnitList* second_prems = UnitList::empty();

    SATClauseStack::Iterator it2(actualSatPremises);
    while (it2.hasNext()) {
      SATClause* scl = it2.next();
      Unit* u;
      Color c = colorFromPossiblyDeepFOConversion(scl,u);

      if (c == sndCol) {
        second.push(scl);
        UnitList::push(u,second_prems);
      } else {
        first.push(scl); // contains first col ones and transparent ones together
        UnitList::push(u,first_prems);
      }
    }

    if (colorCnts[sndCol] == 0) { // this is a degenerate case, in which we don't need to interpolate at all
      Inference* foInf = new InferenceMany(Inference::AVATAR_REFUTATION, first_prems);
      Clause* foRef = Clause::fromIterator(LiteralIterator::getEmpty(), Unit::CONJECTURE, foInf);
      throw MainLoop::RefutationFoundException(foRef);
    }

    SATClauseStack result;
    MinisatInterfacing::interpolateViaAssumptions(_parent.maxSatVar(),first,second,result);

    // turn result into Formula wrapping its CNF structure
    Formula* interpolant;
    {
      FormulaList* conjuncts = FormulaList::empty();
      unsigned conj_cnt = 0;

      SATClauseStack::Iterator it(result);
      while(it.hasNext()) {
        SATClause* cl = it.next();
        FormulaList* disjuncts = FormulaList::empty();

        for (unsigned i = 0; i < cl->size(); i++) {
          SATLiteral lit = (*cl)[i];

          // get the first order clause
          bool negated = false;
          SplitLevel lvl = _parent.getNameFromLiteralUnsafe(lit);
          if (_parent._db[lvl] == 0) {
            negated = true;
            lvl = _parent.getNameFromLiteralUnsafe(lit.opposite());
            ASS_NEQ(_parent._db[lvl],0);
          }
          Formula* litFla = Formula::fromClause(_parent._db[lvl]->component);
          if (negated) {
            litFla = new NegatedFormula(litFla);
          }
          FormulaList::push(litFla,disjuncts);
        }
        Formula* clFla;
        if (cl->size() == 1) {
          clFla = disjuncts->head();
          FormulaList::destroy(disjuncts);
        } else {
          clFla = JunctionFormula::generalJunction(OR, disjuncts);
        }
        FormulaList::push(clFla,conjuncts);
        conj_cnt++;
      }

      if (conj_cnt == 1) {
        interpolant = conjuncts->head();
        FormulaList::destroy(conjuncts);
      } else {
        interpolant = JunctionFormula::generalJunction(AND, conjuncts);
      }
    }

    // finish constructing the derivation
    {
      Inference* elInf = new InferenceMany(Inference::SAT_COLOR_ELIMINATION, second_prems);
      FormulaUnit* interpolated = new FormulaUnit(interpolant,elInf,Unit::CONJECTURE /*do we care about input type at all at this point?*/);

      UnitList::push(interpolated,first_prems);

      Inference* finalInf = new InferenceMany(Inference::SAT_COLOR_ELIMINATION,first_prems);
      Clause* foRef = Clause::fromIterator(LiteralIterator::getEmpty(), Unit::CONJECTURE, finalInf);

      throw MainLoop::RefutationFoundException(foRef);
    }
  }
}

SATSolver::VarAssignment SplittingBranchSelector::getSolverAssimentConsideringCCModel(unsigned var) {
  CALL("SplittingBranchSelector::getSolverAssimentConsideringCCModel");

  if (_ccModel) {
    // if we work with ccModel, the cc-model overrides the satsolver, but only for positive ground equalities
    SAT2FO& s2f = _parent.satNaming();
    Literal* lit = s2f.toFO(SATLiteral(var,true));

    if (lit && lit->isEquality() && lit->ground()) {
      if (_trueInCCModel.find(var)) {
        ASS(_solver->getAssignment(var) != SATSolver::FALSE || var > lastCheckedVar);
        // only a newly introduced variable can be false in the SATSolver for no good reason

        return SATSolver::TRUE;
      }
      // else we can force neither FALSE not DONT_CARE here, because
      // the former could introduce a disequality that shouldn't be in FO anymore
      // and the latter could prevent a removal (if we are not eager)
      // In sum, the model which this function exposes to the outside world
      // must still satisfy all the clauses in _solver !
    }
    // "fall-through" to consult _solver anyway
  }

  return _solver->getAssignment(var);
}

static const unsigned AGE_NOT_FILLED = UINT_MAX;

int SplittingBranchSelector::assertedGroundPositiveEqualityCompomentMaxAge()
{
  CALL("SplittingBranchSelector::assertedGroundPositiveEqualityCompomentMaxAge");

  int max = 0;

  unsigned maxSatVar = _parent.maxSatVar();
  for(unsigned i=1; i<=maxSatVar; i++) {
    SATSolver::VarAssignment asgn = _solver->getAssignment(i);
    if(asgn==SATSolver::DONT_CARE) {
      continue;
    }
    SATLiteral sl(i, asgn==SATSolver::TRUE);
    SplitLevel name = _parent.getNameFromLiteral(sl);
    if (!_parent.isUsedName(name)) {
      continue;
    }
    Clause* compCl = _parent.getComponentClause(name);
    if (compCl->length() != 1) {
      continue;
    }
    Literal* l = (*compCl)[0];
    if (l->ground() && l->isEquality() && l->isPositive()) {
      int clAge = compCl->age();

      if (clAge > max) {
        max = clAge;
      }
    }
  }

  return max;
}

SATSolver::Status SplittingBranchSelector::processDPConflicts()
{
  CALL("SplittingBranchSelector::processDPConflicts");
  // ASS(_solver->getStatus()==SATSolver::SATISFIABLE);

  if(!_dp) {
    return SATSolver::SATISFIABLE;
  }
  
  SAT2FO& s2f = _parent.satNaming();
  static LiteralStack gndAssignment;
  static LiteralStack unsatCore;

  while (true) { // breaks inside
    {
      TimeCounter tc(TC_CONGRUENCE_CLOSURE);
    
      gndAssignment.reset();
      // collects only ground literals, because it known only about them ...
      s2f.collectAssignment(*_solver, gndAssignment); 
      // ... moreover, _dp->addLiterals will filter the set anyway

      _dp->reset();
      _dp->addLiterals(pvi( LiteralStack::ConstIterator(gndAssignment) ));
      DecisionProcedure::Status dpStatus = _dp->getStatus(_ccMultipleCores);

      if(dpStatus!=DecisionProcedure::UNSATISFIABLE) {
        break;
      }

      unsigned unsatCoreCnt = _dp->getUnsatCoreCount();
      for(unsigned i=0; i<unsatCoreCnt; i++) {
        unsatCore.reset();
        _dp->getUnsatCore(unsatCore, i);
        SATClause* conflCl = s2f.createConflictClause(unsatCore);
        if (_minSCO) {
          _solver->addClauseIgnoredInPartialModel(conflCl);
        } else {
          _solver->addClause(conflCl);
        }
      }

      RSTAT_CTR_INC("ssat_dp_conflict");
      RSTAT_CTR_INC_MANY("ssat_dp_conflict_clauses",unsatCoreCnt);
    }

    // there was conflict, so we try looking for a different model
    {
    	TimeCounter tca(TC_SAT_SOLVER);
    	
      if (_solver->solve() == SATSolver::UNSATISFIABLE) {
        return SATSolver::UNSATISFIABLE;
      }
    }
  }
  
  // ASS(_solver->getStatus()==SATSolver::SATISFIABLE);
  if (_ccModel) {
    TimeCounter tc(TC_CCMODEL);

#ifdef VDEBUG
    // to keep track of SAT variables introduce just for the sake of the latest call to _ccModel
    lastCheckedVar = _parent.maxSatVar();
#endif

    RSTAT_CTR_INC("ssat_dp_model");

    static LiteralStack model;
    model.reset();

    _dpModel->reset();
    _dpModel->addLiterals(pvi( LiteralStack::ConstIterator(gndAssignment) ),true /*only equalities now*/);
    ALWAYS(_dpModel->getStatus(false) == DecisionProcedure::SATISFIABLE);
    _dpModel->getModel(model);

    // RSTAT_MCTR_INC("ssat_dp_model_size",model.size());

    _trueInCCModel.reset();

    // cout << "Obtained a model " << endl;
    unsigned parentMaxAge = AGE_NOT_FILLED;
    LiteralStack::Iterator it(model);
    while(it.hasNext()) {
      Literal* lit = it.next();

      // cout << lit->toString() << endl;

      ASS(lit->isPositive());
      ASS(lit->isEquality());
      ASS(lit->ground());

      Clause* compCl;
      SplitLevel level = _parent.tryGetComponentNameOrAddNew(1,&lit,0,compCl);
      if (compCl->age() == AGE_NOT_FILLED) { // added new
        RSTAT_CTR_INC("ssat_dp_model_components");

        if (parentMaxAge == AGE_NOT_FILLED) {
          // This is the max of all the positive ground units that went into the DP.
          // As such, is overestimates that "true age" that could be computed
          // as the max over the true parents of this equality
          // (we are lazy and cannot know the true parents without effort).
          parentMaxAge = assertedGroundPositiveEqualityCompomentMaxAge();
        }

        compCl->setAge(parentMaxAge);

        // we could have actually created two clauses
        unsigned oppLevel = level^1;
        if (_parent.isUsedName(oppLevel)) {
          Clause* negCompCl = _parent.getComponentClause(oppLevel);
          ASS(negCompCl);

          if (negCompCl->age() == AGE_NOT_FILLED) { // it could have age from before, if it was not introduced by ccModel
            ASS(_parent._complBehavior!=Options::SplittingAddComplementary::NONE);  // but only for "ssac = ground"
            negCompCl->setAge(parentMaxAge);
          }
        }
      }

      SATLiteral slit = _parent.getLiteralFromName(level);
      ASS(slit.polarity());
      _trueInCCModel.insert(slit.var());
    }
  }
  
  return SATSolver::SATISFIABLE;
}

void SplittingBranchSelector::updateSelection(unsigned satVar, SATSolver::VarAssignment asgn,
    SplitLevelStack& addedComps, SplitLevelStack& removedComps)
{
  CALL("SplittingBranchSelector::updateSelection");
  ASS_NEQ(asgn, SATSolver::NOT_KNOWN); //we always do full SAT solving, so there shouldn't be unknown variables

  SplitLevel posLvl = _parent.getNameFromLiteral(SATLiteral(satVar, true));
  SplitLevel negLvl = _parent.getNameFromLiteral(SATLiteral(satVar, false));

  switch(asgn) {
  case SATSolver::TRUE: 
    if(!_selected.find(posLvl) && _parent.isUsedName(posLvl)) {
      _selected.insert(posLvl);
      addedComps.push(posLvl);
    }
    if(_selected.find(negLvl)) {
      _selected.remove(negLvl);
      removedComps.push(negLvl);
    }
    break;
  case SATSolver::FALSE:    
    if(!_selected.find(negLvl) && _parent.isUsedName(negLvl)) {
      _selected.insert(negLvl);
      addedComps.push(negLvl);
    }
    if(_selected.find(posLvl)) {
      _selected.remove(posLvl);
      removedComps.push(posLvl);
    }
    break;
  case SATSolver::DONT_CARE:
    if(_eagerRemoval) {
      if(_selected.find(posLvl)) {
        _selected.remove(posLvl);
        removedComps.push(posLvl);
      }
      if(_selected.find(negLvl)) {
        _selected.remove(negLvl);
        removedComps.push(negLvl);
      }
    }
    break;
  default:
    ASSERTION_VIOLATION;
  }
}

void SplittingBranchSelector::addSatClauseToSolver(SATClause* cl, bool branchRefutation)
{
  CALL("SplittingBranchSelector::addSatClauseToSolver");

  cl = Preprocess::removeDuplicateLiterals(cl);
  if(!cl) {
    RSTAT_CTR_INC("splitter_tautology");
    return;
  }

  RSTAT_CTR_INC("ssat_sat_clauses");

  if (branchRefutation && _minSCO) {
    _solver->addClauseIgnoredInPartialModel(cl);
  } else {
    _solver->addClause(cl);
  }
}

void SplittingBranchSelector::recomputeModel(SplitLevelStack& addedComps, SplitLevelStack& removedComps, bool randomize)
{
  CALL("SplittingBranchSelector::recomputeModel");
  ASS(addedComps.isEmpty());
  ASS(removedComps.isEmpty());

  unsigned maxSatVar = _parent.maxSatVar();
  
  SATSolver::Status stat;
  {
    TimeCounter tc1(TC_SAT_SOLVER);
    if (randomize) {
      _solver->randomizeForNextAssignment(maxSatVar);
    }
    stat = _solver->solve();
  }
  if (stat == SATSolver::SATISFIABLE) {
    stat = processDPConflicts();
  }
  if(stat == SATSolver::UNSATISFIABLE) {
    handleSatRefutation(); // noreturn!
  }
  if(stat == SATSolver::UNKNOWN){
    env.statistics->smtReturnedUnknown=true;
    throw MainLoop::MainLoopFinishedException(Statistics::REFUTATION_NOT_FOUND);
  }
  ASS_EQ(stat,SATSolver::SATISFIABLE);

  unsigned _usedcnt=0; // for the statistics below
  for(unsigned i=1; i<=maxSatVar; i++) {
    SATSolver::VarAssignment asgn = getSolverAssimentConsideringCCModel(i);
    updateSelection(i, asgn, addedComps, removedComps);
    
    if (asgn != SATSolver::DONT_CARE) {
      _usedcnt++;
    }
  }
  /*
  if(maxSatVar>=1){
    int percent = (_usedcnt *100) / maxSatVar;
    RSTAT_MCTR_INC("minimise_model_percent",percent);
  }
  
  RSTAT_CTR_INC_MANY("ssat_usual_activations", addedComps.size());
  RSTAT_CTR_INC_MANY("ssat_usual_deactivations", removedComps.size());
  */
}

//////////////
// Splitter
//////////////

vstring Splitter::splPrefix = "";

Splitter::Splitter()
: _deleteDeactivated(Options::SplittingDeleteDeactivated::ON), _branchSelector(*this),
  _clausesAdded(false), _haveBranchRefutation(false)
{
  CALL("Splitter::Splitter");
  if(env.options->proof()==Options::Proof::TPTP){
    unsigned spl = env.signature->addFreshFunction(0,"spl");
    splPrefix = env.signature->functionName(spl)+"_";
  }
}

Splitter::~Splitter()
{
  CALL("Splitter::~Splitter");

  while(_db.isNonEmpty()) {
    if(_db.top()) {
      delete _db.top();
    }
    _db.pop();
  }
}

const Options& Splitter::getOptions() const
{
  CALL("Splitter::getOptions");
  ASS(_sa);

  return _sa->getOptions();
}

Ordering& Splitter::getOrdering() const
{
  CALL("Splitter::getOrdering");
  ASS(_sa);

  return _sa->getOrdering();
}


void Splitter::init(SaturationAlgorithm* sa)
{
  CALL("Splitter::init");

  _sa = sa;

  const Options& opts = getOptions();
  _branchSelector.init();

  _showSplitting = opts.showSplitting();

  _complBehavior = opts.splittingAddComplementary();
  _nonsplComps = opts.splittingNonsplittableComponents();

  _flushPeriod = opts.splittingFlushPeriod();
  _flushQuotient = opts.splittingFlushQuotient();
  _flushThreshold = sa->getGeneratedClauseCount() + _flushPeriod;
  _congruenceClosure = opts.splittingCongruenceClosure();
#if VZ3
  hasSMTSolver = (opts.satSolver() == Options::SatSolver::Z3);
#endif

  _fastRestart = opts.splittingFastRestart();
  _deleteDeactivated = opts.splittingDeleteDeactivated();

  if (opts.useHashingVariantIndex()) {
    _componentIdx = new HashingClauseVariantIndex();
  } else {
    _componentIdx = new SubstitutionTreeClauseVariantIndex();
  }
}

SplitLevel Splitter::getNameFromLiteral(SATLiteral lit) const
{
  CALL("Splitter::getNameFromLiteral");

  SplitLevel res = getNameFromLiteralUnsafe(lit);
  ASS_L(res, _db.size());
  return res;
}

/**
 * This function can be called with SAT literal for which the split
 * record is not created yet. In this case the result will be larger
 * than the size of _db.
 */
SplitLevel Splitter::getNameFromLiteralUnsafe(SATLiteral lit) const
{
  CALL("Splitter::getNameFromLiteralUnsafe");

  return (lit.var()-1)*2 + (lit.polarity() ? 0 : 1);
}
SATLiteral Splitter::getLiteralFromName(SplitLevel compName) const
{
  CALL("Splitter::getLiteralFromName");
  ASS_L(compName, _db.size());

  unsigned var = compName/2 + 1;
  bool polarity = (compName&1)==0;
  return SATLiteral(var, polarity);
}
Unit* Splitter::getDefinitionFromName(SplitLevel compName) const
{
  CALL("Splitter::getDefinitionFromName");

  Unit* def;
  ALWAYS(_defs.find(compName,def));
  return def;
}

void Splitter::collectDependenceLits(SplitSet* splits, SATLiteralStack& acc) const
{
  SplitSet::Iterator sit(*splits);
  while(sit.hasNext()) {
    SplitLevel nm = sit.next();
    acc.push(getLiteralFromName(nm).opposite());
  }
}

Clause* Splitter::getComponentClause(SplitLevel name) const
{
  CALL("Splitter::getComponentClause");
  ASS_L(name,_db.size());
  ASS_NEQ(_db[name],0);

  return _db[name]->component;
}

void Splitter::onAllProcessed()
{
  CALL("Splitter::onAllProcessed");

  bool flushing = false;
  if(_flushPeriod) {
    if(_haveBranchRefutation) {
      _flushThreshold = _sa->getGeneratedClauseCount()+_flushPeriod;
    }
    if(_sa->getGeneratedClauseCount()>=_flushThreshold && !_clausesAdded) {
      flushing = true;
      _flushThreshold = _sa->getGeneratedClauseCount()+_flushPeriod;
      _flushPeriod = static_cast<unsigned>(_flushPeriod*_flushQuotient);
    }
  }

  _haveBranchRefutation = false;
  if(!_clausesAdded && !flushing) {
    return;
  }
  _clausesAdded = false;

  static SplitLevelStack toAdd;
  static SplitLevelStack toRemove;
  
  toAdd.reset();
  toRemove.reset();  

  _branchSelector.recomputeModel(toAdd, toRemove, flushing);
  
  if (_showSplitting) { // TODO: this is just one of many ways Splitter could report about changes
    env.beginOutput();
    env.out() << "[AVATAR] recomputeModel: +";
    for (unsigned i = 0; i < toAdd.size(); i++) {
      env.out() << toAdd[i] << ",";
    }
    env.out() << " -";
    for (unsigned i = 0; i < toRemove.size(); i++) {
      env.out() << toRemove[i] << ",";
    }
    env.out() << std::endl;
    env.endOutput();
  }

  {
    TimeCounter tc(TC_SPLITTING_MODEL_UPDATE); // includes component removals and additions, also processing fast clauses and zero implied splits

    if(toRemove.isNonEmpty()) {
      removeComponents(toRemove);
    }
    if(toAdd.isNonEmpty()) {
      addComponents(toAdd);
    }

    // now that new activ-ness has been determined
    // we can put back the fast clauses, if any
    while(_fastClauses.isNonEmpty()) {
      Clause* rcl=_fastClauses.popWithoutDec();

      // TODO: could use a check based on "NumActiveSplits" instead,
      // but would need to maintain them even when _deleteDeactivated == Options::SplittingDeleteDeactivated::ON
      if (allSplitLevelsActive(rcl->splits())) {
        RSTAT_CTR_INC("fast_clauses_restored");
        _sa->addNewClause(rcl);
      } else {
        RSTAT_CTR_INC("fast_clauses_not_restored");
      }

      rcl->decRefCnt(); //belongs to _fastClauses.popWithoutDec();
    }
  }
}


bool Splitter::shouldAddClauseForNonSplittable(Clause* cl, unsigned& compName, Clause*& compCl)
{
  CALL("Splitter::shouldAddClauseForNonSplittable");
  
  //!! this check is important or we might end up looping !!
  if(cl->isComponent()) {    
    return false;
  }

  if((_congruenceClosure != Options::SplittingCongruenceClosure::OFF
#if VZ3
      || hasSMTSolver
#endif
      )
      && cl->length()==1 && (*cl)[0]->ground() ) {
    //we add ground unit clauses if we use congruence closure...
    // (immediately zero implied!)
    compName = tryGetComponentNameOrAddNew(cl->length(), cl->literals(), cl, compCl);
    RSTAT_CTR_INC("ssat_ground_clauses_for_congruence");
    return true;
  }

  if(_nonsplComps==Options::SplittingNonsplittableComponents::NONE) {
    return false;
  }

  if(!tryGetExistingComponentName(cl->length(), cl->literals(), compName, compCl)) {
    bool canCreate;
    switch(_nonsplComps) {
    case Options::SplittingNonsplittableComponents::ALL:
      canCreate = true;
      break;
    case Options::SplittingNonsplittableComponents::ALL_DEPENDENT:
      canCreate = !cl->splits()->isEmpty();
      break;
    case Options::SplittingNonsplittableComponents::KNOWN:
      canCreate = false;
      break;
    default:
      ASSERTION_VIOLATION;
    }
    if(!canCreate) {
      return false;
    }
    RSTAT_CTR_INC("ssat_non_splittable_introduced_components");
    compName = tryGetComponentNameOrAddNew(cl->length(), cl->literals(), cl, compCl);
  }
  ASS_NEQ(cl,compCl);

  // We only reach here if cl already exists as a component

  return true;
}

bool Splitter::handleNonSplittable(Clause* cl)
{
  CALL("Splitter::handleNonSplittable");

  SplitLevel compName;
  Clause* compCl;
  if(!shouldAddClauseForNonSplittable(cl, compName, compCl)) {
    return false;
  }

  // OK, we will handle the clause, this means for the FO part we will pretend it was redundant
  // and instead we will record information about it in the SAT solver

  SplitRecord& nameRec = *_db[compName];
  ASS_EQ(nameRec.component,compCl);
  ASS_REP2(compCl->store()==Clause::NONE || compCl->store()==Clause::ACTIVE ||
      compCl->store()==Clause::PASSIVE || compCl->store()==Clause::UNPROCESSED, *compCl, compCl->store());

  if(nameRec.active && compCl->store()==Clause::NONE) {
    //we need to make sure the clause naming the component is present in this case, as the
    //following scenario may lead to incompleteness:
    //  component C is selected and put to unprocessed
    //  clause C' syntactically equal to C is derived and put into simplification container
    //  component C is made redundant by C'
    //  we name C' as C. The sat clause {C} won't lead to addition of C into FO as C is already selected.

    compCl->invalidateMyReductionRecords();
    _sa->addNewClause(compCl);
    if ((_deleteDeactivated != Options::SplittingDeleteDeactivated::ON) &&
            !nameRec.children.find(compCl)) {
      // corner case within a corner case:
      // the compCl was already shown unconditionally redundant,
      // but now we must must put it back (TODO: do we really?)
      // so we must also keep track of it
      nameRec.children.push(compCl);
    }
  }

  SplitSet* sset = cl->splits();
  ASS(sset->size()!=1 || _db[sset->sval()]->component!=cl);
  if(sset->member(compName)) {
    //we derived a component that depends on itself.
    //This derivation is therefore redundant, so we can skip it.
    // (would result in a propositional tautology)

    RSTAT_CTR_INC("ssat_self_dependent_component");
  } else {
    static SATLiteralStack satLits;
    satLits.reset();
    collectDependenceLits(cl->splits(), satLits);
    satLits.push(getLiteralFromName(compName));

    SATClause* nsClause = SATClause::fromStack(satLits);

    UnitList* ps = 0;

    FormulaList* resLst=0;
    // do compName first
    UnitList::push(getDefinitionFromName(compName),ps);
    vstring compNameNm = splPrefix+Lib::Int::toString(compName);
    if((compName&1)!=0){ compNameNm="~"+compNameNm; }
    FormulaList::push(new NamedFormula(compNameNm),resLst);
 
    // now do splits
    SplitSet::Iterator sit(*cl->splits());
    while(sit.hasNext()) {
      SplitLevel nm = sit.next();
      UnitList::push(getDefinitionFromName(nm),ps);
      vstring lnm = splPrefix+Lib::Int::toString(nm);
      // In the splits we reverse polarity
      if((nm&1)==0){ lnm="~"+lnm; }
      FormulaList::push(new NamedFormula(lnm),resLst);
    }

    UnitList::push(cl,ps); // making sure this clause is the last one pushed (for the sake of colorFromAssumedFOConversion)

    Formula* f = JunctionFormula::generalJunction(OR,resLst);
    FormulaUnit* scl = new FormulaUnit(f,new InferenceMany(Inference::AVATAR_SPLIT_CLAUSE,ps),cl->inputType());

    nsClause->setInference(new FOConversionInference(scl));

    if (_showSplitting) {
      env.beginOutput();
      env.out() << "[AVATAR] registering a non-splittable: "<< cl->toString() << std::endl;
      env.endOutput();
    }

    addSatClauseToSolver(nsClause, false);

    RSTAT_CTR_INC("ssat_non_splittable_sat_clauses");
  }

  return true;
}

/**
 * Takes Clause cl and attempts to split it into Components i.e. produces C1...Cn = cl such that
 * all Ci's have a pairwise disjoint set of variables and no Ci can be split further - the
 * splitting is maximal.
 *
 * Returns true if this is possible and false otherwise. The result is placed in acc.
 *
 * This is implemented using the Union-Find algorithm.
 *
 * Comment by Giles. 
 */
bool Splitter::getComponents(Clause* cl, Stack<LiteralStack>& acc)
{
  CALL("Splitter::getComponents");
  ASS_EQ(acc.size(), 0);

  unsigned clen=cl->length();
  ASS_G(clen,0);

  if(clen<=1) {
    return false;
  }

  //Master literal of an variable is the literal
  //with lowest index, in which it appears.
  static DHMap<unsigned, unsigned, IdentityHash> varMasters;
  varMasters.reset();
  IntUnionFind components(clen);

  for(unsigned i=0;i<clen;i++) {
    Literal* lit=(*cl)[i];
    VariableIterator vit(lit);
    while(vit.hasNext()) {
      unsigned master=varMasters.findOrInsert(vit.next().var(), i);
      if(master!=i) {
	components.doUnion(master, i);
      }
    }
  }
  components.evalComponents();

  unsigned compCnt=components.getComponentCount();

  if(compCnt==1) {
    return false;
  }

  env.statistics->splitClauses++;
  env.statistics->splitComponents+=compCnt;

  IntUnionFind::ComponentIterator cit(components);
  ASS(cit.hasNext());
  while(cit.hasNext()) {
    IntUnionFind::ElementIterator elit=cit.next();

    acc.push(LiteralStack());

    while(elit.hasNext()) {
      int litIndex=elit.next();
      Literal* lit = (*cl)[litIndex];

      acc.top().push(lit);
    }
  }
  ASS_EQ(acc.size(),compCnt);
  return true;
}


/**
 * Attempt to split clause @b cl, and return true if successful
 */
bool Splitter::doSplitting(Clause* cl)
{
  CALL("Splitter::doSplitting");

  if (_fastRestart && _haveBranchRefutation) {
    _fastClauses.push(cl);
    return true; // the clause is ours now
  }

  static Stack<LiteralStack> comps;
  comps.reset();
  // fills comps with components, returning if not splittable
  if(!getComponents(cl, comps)) {
    return handleNonSplittable(cl);
  }

  static SATLiteralStack satClauseLits;
  satClauseLits.reset();

  // Add literals for existing constraints 
  collectDependenceLits(cl->splits(), satClauseLits);

  UnitList* ps = 0;
  FormulaList* resLst=0;

  unsigned compCnt = comps.size();
  for(unsigned i=0; i<compCnt; ++i) {
    const LiteralStack& comp = comps[i];
    Clause* compCl;
    SplitLevel compName = tryGetComponentNameOrAddNew(comp, cl, compCl);
    SATLiteral nameLit = getLiteralFromName(compName);
    satClauseLits.push(nameLit);

    UnitList::push(getDefinitionFromName(compName),ps);
    vstring compNameNm = splPrefix+Lib::Int::toString(compName);
    if((compName&1)!=0){ compNameNm="~"+compNameNm; }
    FormulaList::push(new NamedFormula(compNameNm),resLst);
  }

  SATClause* splitClause = SATClause::fromStack(satClauseLits);

  if (_showSplitting) {
    env.beginOutput();
    env.out() << "[AVATAR] split a clause: "<< cl->toString() << std::endl;
    env.endOutput();
  }

  // now do splits
  SplitSet::Iterator sit(*cl->splits());
  while(sit.hasNext()) {
    SplitLevel nm = sit.next();
    UnitList::push(getDefinitionFromName(nm),ps);
    vstring lnm = splPrefix+Lib::Int::toString(nm);
    // in the splits we reverse polarity
    if((nm&1)==0){ lnm="~"+lnm; }
    FormulaList::push(new NamedFormula(lnm),resLst);
  }

  UnitList::push(cl,ps); // making sure this clause is the last one pushed (for the sake of colorFromAssumedFOConversion)

  Formula* f = JunctionFormula::generalJunction(OR,resLst);
  FormulaUnit* scl = new FormulaUnit(f,new InferenceMany(Inference::AVATAR_SPLIT_CLAUSE,ps),cl->inputType());

  splitClause->setInference(new FOConversionInference(scl));

  addSatClauseToSolver(splitClause, false);

  env.statistics->satSplits++;
  return true;
}

/**
 * Uses _componentIdx to search for an existing name for the component represented by @b lits
 *
 * @param size number of literals in component
 * @param lits literals of component
 * @param comp the existing propositional name (SplitLevel) for this component - to be filled 
 * @param compCl the existing clause for this component - to be filled
 * @return True if the component already exists
 *
 * @author Giles
 */
bool Splitter::tryGetExistingComponentName(unsigned size, Literal* const * lits, SplitLevel& comp, Clause*& compCl)
{
  CALL("Splitter::tryGetExistingComponentName");

  ClauseIterator existingComponents;
  { 
    TimeCounter tc(TC_SPLITTING_COMPONENT_INDEX_USAGE);
    existingComponents = _componentIdx->retrieveVariants(lits, size);
  }

  if(!existingComponents.hasNext()) {
    return false;
  }
  compCl = existingComponents.next();
  ASS(!existingComponents.hasNext());
  comp = _compNames.get(compCl);
  return true;
}

/**
 * Records a new component. This involves
 * - Building a new Clause for the component as a AVATAR_COMPONENT
 * - Create a SplitRecord for the component
 * - Record the name in the splits of the clause
 * - Insert the clause into _componentIdx for variant checking later
 * - Insert the clause with the name into _compNames for lookup later
 *
 * @param name The propositional name for the component to add
 * @param size The number of literals in the component to add
 * @param lits The literals in the component to add
 * @param orig The original clause i.e. the one that we are splitting
 *
 * Comment by Giles.
 */
Clause* Splitter::buildAndInsertComponentClause(SplitLevel name, unsigned size, Literal* const * lits, Clause* orig)
{
  CALL("Splitter::buildAndInsertComponentClause");
  ASS_EQ(_db[name],0);

  Unit::InputType inpType = orig ? orig->inputType() : Unit::AXIOM;

  Clause* temp = Clause::fromIterator(getArrayishObjectIterator(lits, size), inpType,new Inference(Inference::AVATAR_DEFINITION));
  Formula* def_f = new BinaryFormula(IFF,
               new NamedFormula(splPrefix+Lib::Int::toString(name)),
               Formula::fromClause(temp));

  FormulaUnit* def_u = new FormulaUnit(def_f,new Inference(Inference::AVATAR_DEFINITION),inpType);
  InferenceStore::instance()->recordIntroducedSplitName(def_u,splPrefix+Lib::Int::toString(name));
  //cout << "Add def for " << def_u->toString() << endl;
  ALWAYS(_defs.insert(name,def_u));

  Clause* compCl = Clause::fromIterator(getArrayishObjectIterator(lits, size), inpType, 
          new Inference1(Inference::AVATAR_COMPONENT,def_u));

  if(orig && orig->isTheoryDescendant()){ compCl->setTheoryDescendant(true); }
  if(orig){ compCl->setInductionDepth(orig->inductionDepth()); }

  //cout << "Name " << getLiteralFromName(name).toString() << " for " << compCl->toString() << endl; 

  compCl->setAge(orig ? orig->age() : AGE_NOT_FILLED);

  _db[name] = new SplitRecord(compCl);
  compCl->setSplits(SplitSet::getSingleton(name));
  compCl->setComponent(true);

  if (_deleteDeactivated != Options::SplittingDeleteDeactivated::ON) {
    // in this mode, compCl is assumed to be a child since the beginning of times
    _db[name]->children.push(compCl);
    
    // (with _deleteDeactivated on, compCl is always inserted anew on activation)
  }
  
  {
    TimeCounter tc(TC_SPLITTING_COMPONENT_INDEX_MAINTENANCE);
    _componentIdx->insert(compCl);
  }
  _compNames.insert(compCl, name);

  if(orig && env.clausePriorities){
    env.clausePriorities->insert(compCl,orig->getPriority());
  }

  return compCl;
}

SplitLevel Splitter::addNonGroundComponent(unsigned size, Literal* const * lits, Clause* orig, Clause*& compCl)
{
  CALL("Splitter::addNonGroundComponent");
  ASS_REP(_db.size()%2==0, _db.size());
  ASS_G(size,0);
  ASS(forAll(getArrayishObjectIterator(lits, size), 
          [] (Literal* l) { return !l->ground(); } )); //none of the literals can be ground

  SATLiteral posLit(_sat2fo.createSpareSatVar(), true);
  SplitLevel compName = getNameFromLiteralUnsafe(posLit);
  ASS_EQ(compName&1,0); //positive levels are even
  ASS_GE(compName,_db.size());
  _db.push(0);
  _db.push(0);
  ASS_L(compName,_db.size());

  _branchSelector.updateVarCnt();
  _branchSelector.considerPolarityAdvice(posLit);

  compCl = buildAndInsertComponentClause(compName, size, lits, orig);

  return compName;
}

SplitLevel Splitter::addGroundComponent(Literal* lit, Clause* orig, Clause*& compCl)
{
  CALL("Splitter::addGroundComponent");
  ASS_REP(_db.size()%2==0, _db.size());
  ASS(lit->ground());

  SATLiteral satLit = _sat2fo.toSAT(lit);
  SplitLevel compName = getNameFromLiteralUnsafe(satLit);

  if(compName>=_db.size()) {
    _db.push(0);
    _db.push(0);
  }
  else {
    ASS_EQ(_complBehavior,Options::SplittingAddComplementary::NONE); 
    //otherwise the complement would have been created below ...
    // ... in the respective previous pass through this method 
  }
  ASS_L(compName,_db.size());

  if(_complBehavior!=Options::SplittingAddComplementary::NONE) {
    //we insert both literal and its negation
    unsigned oppName = compName^1;
    ASS_L(oppName,_db.size());
    Literal* opposite = Literal::complementaryLiteral(lit);
    buildAndInsertComponentClause(oppName, 1, &opposite, orig);
  }
  compCl = buildAndInsertComponentClause(compName, 1, &lit, orig);

  _branchSelector.updateVarCnt();
  _branchSelector.considerPolarityAdvice(satLit);

  return compName;
}

/**
 * @param orig original clause (one being split) used to determine input type of the component.
 *             Can be zero, in that case the input type is Unit::AXIOM
 * @param comp Component (Record) that we're getting the name for
 * @param compCl The clause that will be used to represent this component - to be filled
 *
 * @return the propositional name for the Clause (to be passed to the SAT solver)
 */
SplitLevel Splitter::tryGetComponentNameOrAddNew(const LiteralStack& comp, Clause* orig, Clause*& compCl)
{
  CALL("Splitter::getComponentName/3");
  return tryGetComponentNameOrAddNew(comp.size(), comp.begin(), orig, compCl);
}

/**
 * @param orig original clause (one being split) used to determine input type of the component.
 *             Can be zero, in that case the input type is Unit::AXIOM
 * @param size The number of literals in the component
 * @param lits The component to be named (as an array of literals)
 * @param compCl The clause that will be used to represent this component - to be filled
 *
 * @return the propositional name for the Clause (to be passed to the SAT solver) 
 */
SplitLevel Splitter::tryGetComponentNameOrAddNew(unsigned size, Literal* const * lits, Clause* orig, Clause*& compCl)
{
  CALL("Splitter::getComponentName/4");

  SplitLevel res;

  if(tryGetExistingComponentName(size, lits, res, compCl)) {
    RSTAT_CTR_INC("ssat_reused_components");
  }
  else {
    RSTAT_CTR_INC("ssat_new_components");

    if(size==1 && lits[0]->ground()) {
      res = addGroundComponent(lits[0], orig, compCl);
    }
    else {
      res = addNonGroundComponent(size, lits, orig, compCl);
    }
  }
  return res;
}

static const int NOT_WORTH_REINTRODUCING = 0;

/**
 * Assign the @b SplitSet @b splits to the clause @b cl.
 */
void Splitter::assignClauseSplitSet(Clause* cl, SplitSet* splits)
{
  CALL("Splitter::assignClauseSplitSet");
  ASS(!cl->splits());
    
  cl->setSplits(splits);

  //update "children" field of relevant SplitRecords
  SplitSet::Iterator bsit(*splits);
  bool should_reintroduce = false;
  unsigned cl_weight = cl->weight();
  while(bsit.hasNext()) {
    SplitLevel slev=bsit.next();
    _db[slev]->children.push(cl);    
    if (cl_weight <= _db[slev]->component->weight()) {
      should_reintroduce = true;
    }
  }  
  
  /**
   * Heuristic idea -- only if the clause is lighter than at least
   * one of the component clauses on which it depends, 
   * it will be kept for reintroduction.
   */
  if (_deleteDeactivated != Options::SplittingDeleteDeactivated::ON) {
    cl->setNumActiveSplits(
      (_deleteDeactivated == Options::SplittingDeleteDeactivated::OFF || should_reintroduce) ? 
        splits->size() : NOT_WORTH_REINTRODUCING);
  }
}

/**
 * Register the reduction of the @b cl clause
 *
 * At this stage we also check for zero-implied literals and remove
 * them if found, this is safe as we no longer rely on them
 */
void Splitter::onClauseReduction(Clause* cl, ClauseIterator premises, Clause* replacement)
{
  CALL("Splitter::onClauseReduction");
  ASS(cl);

  if(!premises.hasNext()) {
    ASS(!replacement || cl->splits()==replacement->splits());
    return;
  }

  SplitSet* unionAll;
  if(replacement) {
    unionAll = replacement->splits();
    ASS(forAll(premises, 
            [replacement,this] (Clause* premise) { 
              //SplitSet* difference = premise->splits()->subtract(replacement->splits());
              //if(difference->isEmpty()) return true; // isSubsetOf true
              // Now check if those in the difference are zero implied
              //SplitSet::Iterator dsit(*difference);
              //while(dsit.hasNext()){
              //  SplitLevel sl = dsit.next();
                // check if zero-implied
              //  SATLiteral sat_lit = getLiteralFromName(sl);
              //  if(!_branchSelector.isZeroImplied(sat_lit)) return false;
              //}
              //return true; // all okay              
              return premise->splits()->isSubsetOf(replacement->splits()); 
            } ));
  } else {
    Clause* premise0 = premises.next();
    unionAll=premise0->splits();
    while(premises.hasNext()) {
      Clause* premise=premises.next();
      ASS(premise);
      unionAll=unionAll->getUnion(premise->splits());
    }
  }
  SplitSet* diff=unionAll->subtract(cl->splits());      
        
  ASS(allSplitLevelsActive(diff));

  if(diff->isEmpty()) {
    // unconditionally reduced
    if (_deleteDeactivated != Options::SplittingDeleteDeactivated::ON) {
      if (!cl->isComponent() || _deleteDeactivated == Options::SplittingDeleteDeactivated::OFF) {
        // if it is a component we must keep it, unless we plan to reintroduce all

        // let others know not to keep the clause in children
        cl->setNumActiveSplits(NOT_WORTH_REINTRODUCING);
      }
    }
        
    return;
  }
  // else freeze clause

  // TODO: keep statistics in release ?
//#if VDEBUG
  cl->incFreezeCount();
  // RSTAT_MCTR_INC("frozen clauses",cl->getFreezeCount());
  RSTAT_CTR_INC("total_frozen");
//#endif

  cl->invalidateMyReductionRecords();
  SplitSet::Iterator dit(*diff);
  while(dit.hasNext()) {
    SplitLevel slev=dit.next();
    _db[slev]->addReduced(cl);
  }
}

bool Splitter::allSplitLevelsActive(SplitSet* s)
{
  CALL("Splitter::allSplitLevelsActive");

  SplitSet::Iterator sit(*s);
  while(sit.hasNext()) {
    SplitLevel lev=sit.next();
    ASS_REP(lev<_db.size(), lev);
    ASS_REP(_db[lev]!=0, lev);
    if (!_db[lev]->active) {
      return false;
    }
  }
  return true;
}

void Splitter::onNewClause(Clause* cl)
{
  CALL("Splitter::onNewClause");

  //For now just record if cl is in the variant index
  // i.e. is a component
  //TODO - if it is then
  // (a) if it is true it can be immediately frozen
  // (b) if it is false it can be immediately passed to the SAT
  //      solver and kill the current model
  //bool isComponent = false;
  //{
  //  //TODO - would it be better to use tryGetExistingComponent here?
  //  TimeCounter tc(TC_SPLITTING_COMPONENT_INDEX_USAGE);
  //  isComponent = _componentIdx->retrieveVariants(cl).hasNext();
  //}
  //if(isComponent){
  //	RSTAT_CTR_INC("New Clause is a Component");
  //}

  if(!cl->splits()) {
    SplitSet* splits=getNewClauseSplitSet(cl);
    assignClauseSplitSet(cl, splits);
  }

  if (env.colorUsed) {
    SplitSet* splits = cl->splits();

    Color color = cl->color();

    SplitSet::Iterator it(*splits);
    while(it.hasNext()) {
      SplitLevel lv=it.next();
      SplitRecord* sr=_db[lv];

      color = static_cast<Color>(color | sr->component->color());
    }

    cl->updateColor(color);
  }

  ASS(allSplitLevelsActive(cl->splits()));  
}

/**
 * Return a split set of a new clause
 *
 * Assumes that clauses referred to by cl->inference() object
 * are actual premises of @b cl. (This holds when BDDs are not
 * used.)
 */
SplitSet* Splitter::getNewClauseSplitSet(Clause* cl)
{
  CALL("Splitter::getNewClauseSplitSet");

  SplitSet* res;
  Inference* inf=cl->inference();
  Inference::Iterator it=inf->iterator();

  res=SplitSet::getEmpty();

  while(inf->hasNext(it)) {
    Unit* premu=inf->next(it);
    if(!premu->isClause()) {
      //the premise comes from preprocessing
      continue;
    }
    Clause* prem=static_cast<Clause*>(premu);
    if(!prem->splits()) {
      //the premise comes from preprocessing
      continue;
    }
    res=res->getUnion(prem->splits());
  }
  return res;
}


Splitter::SplitRecord::~SplitRecord()
{
  CALL("Splitter::SplitRecord::~SplitRecord");
  component->decRefCnt();
  while(reduced.isNonEmpty()) {
    Clause* cl = reduced.pop().clause;
    cl->decRefCnt();
  }
}

/**
 * Add a reduced clause to the @b SplitRecord object.
 */
void Splitter::SplitRecord::addReduced(Clause* cl)
{
  CALL("Splitter::SplitRecord::addReduced");

  cl->incRefCnt(); //dec when popped from the '_db[slev]->reduced' stack in backtrack method
  reduced.push(ReductionRecord(cl));
}

void Splitter::addSatClauseToSolver(SATClause* cl, bool refutation) {
  CALL("Splitter::addSatClauseToSolver");

  _clausesAdded = true;
  if (refutation) {
    _haveBranchRefutation = true;
  }
  _branchSelector.addSatClauseToSolver(cl,refutation);
}

bool Splitter::handleEmptyClause(Clause* cl)
{
  CALL("Splitter::handleEmptyClause");

  if(cl->splits()->isEmpty()) {
    return false;
  }

  static SATLiteralStack conflictLits;
  conflictLits.reset();

  collectDependenceLits(cl->splits(), conflictLits);
  SATClause* confl = SATClause::fromStack(conflictLits);

  FormulaList* resLst=0;

  SplitSet::Iterator sit(*cl->splits());
  while(sit.hasNext()) {
    SplitLevel nm = sit.next();
    vstring lnm = splPrefix+Lib::Int::toString(nm);
    // in the splits we reverse polarity
    if((nm&1)==0){ lnm="~"+lnm; }
    FormulaList::push(new NamedFormula(lnm),resLst);
  }

  UnitList* ps = UnitList::empty();
  UnitList::push(cl,ps);

  Formula* f = JunctionFormula::generalJunction(OR,resLst);
  FormulaUnit* scl = new FormulaUnit(f,new InferenceMany(Inference::AVATAR_CONTRADICTION_CLAUSE,ps),cl->inputType());

  confl->setInference(new FOConversionInference(scl));
  
  // RSTAT_MCTR_INC("sspl_confl_len", confl->length());

  addSatClauseToSolver(confl,true);

  env.statistics->satSplitRefutations++;
  return true;
}


void Splitter::addComponents(const SplitLevelStack& toAdd)
{
  CALL("Splitter::addComponents");

  SplitLevelStack::ConstIterator slit(toAdd);
  while(slit.hasNext()) {
    SplitLevel sl = slit.next();
    SplitRecord* sr = _db[sl];
    ASS(sr);
    ASS(!sr->active);
    sr->active = true;
    
    if (_deleteDeactivated == Options::SplittingDeleteDeactivated::ON) {
      ASS(sr->children.isEmpty());
      //we need to put the component clause among children, 
      //so that it is backtracked when we remove the component
      sr->children.push(sr->component);
      _sa->addNewClause(sr->component);
    } else {
      // children were kept, so we just put them back
      RCClauseStack::Iterator chit(sr->children);
      unsigned reactivated_cnt = 0;
      while (chit.hasNext()) {
        Clause* cl = chit.next();
        cl->incNumActiveSplits();
        if (cl->getNumActiveSplits() == (int)cl->splits()->size()) {
          reactivated_cnt++;
          _sa->addNewClause(cl);
          //check that restored clause does not depend on inactive splits
          ASS(allSplitLevelsActive(cl->splits()));
        }
      }
      // RSTAT_MCTR_INC("reactivated clauses",reactivated_cnt);
    }
  }
}

/**
 * Perform backtracking of split levels in @c toRemove.
 *
 * Can be called only when there are no unprocessed clauses left.
 * This is to allow for easy clause removal from the saturation algorithm.
 */
void Splitter::removeComponents(const SplitLevelStack& toRemove)
{
  CALL("Splitter::removeComponents");
  ASS(_sa->clausesFlushed());
  
  SplitSet* backtracked = SplitSet::getFromArray(toRemove.begin(), toRemove.size());

  // ensure all children are backtracked
  // i.e. removed from _sa and reference counter dec
  SplitSet::Iterator blit(*backtracked);
  while(blit.hasNext()) {
    SplitLevel bl=blit.next();
    SplitRecord* sr=_db[bl];
    ASS(sr);
    
    RCClauseStack::DelIterator chit(sr->children);
    while (chit.hasNext()) {
      Clause* ccl=chit.next();
      ASS(ccl->splits()->member(bl));
      if(ccl->store()!=Clause::NONE) {
        _sa->removeActiveOrPassiveClause(ccl);
        ASS_EQ(ccl->store(), Clause::NONE);
      }
      ccl->invalidateMyReductionRecords();
      ccl->decNumActiveSplits();
      if (ccl->getNumActiveSplits() < NOT_WORTH_REINTRODUCING) {
        RSTAT_CTR_INC("unworthy child removed");
        chit.del();
      }
    }
    
    if (_deleteDeactivated == Options::SplittingDeleteDeactivated::ON) {
      sr->children.reset();
    }
  }

  // perform unfreezing  
    
  // pick all reduced clauses (if the record relates to most recent reduction)
  // and them add back to _sa using addNewClause - this will get put to unprocessed
  SplitSet::Iterator blit2(*backtracked);
  while(blit2.hasNext()) {
    SplitLevel bl=blit2.next();
    SplitRecord* sr=_db[bl];

    while(sr->reduced.isNonEmpty()) {
      ReductionRecord rrec=sr->reduced.pop();
      Clause* rcl=rrec.clause;
      if(rcl->validReductionRecord(rrec.timestamp)) {
        ASS(!rcl->splits()->hasIntersection(backtracked));      
        ASS_EQ(rcl->store(), Clause::NONE);
        
        rcl->invalidateMyReductionRecords(); // to make sure we don't unfreeze this clause a second time
        _sa->addNewClause(rcl);
              
        // TODO: keep statistics in release ?
        // RSTAT_MCTR_INC("unfrozen clauses",rcl->getFreezeCount());
        RSTAT_CTR_INC("total_unfrozen");
#if VDEBUG      
        //check that restored clause does not depend on inactive splits
        ASS(allSplitLevelsActive(rcl->splits()));
#endif
        
      }
      rcl->decRefCnt(); //inc when pushed on the 'sr->reduced' stack in Splitter::SplitRecord::addReduced
    }

    ASS(sr->active);
    sr->active = false;
  }
}

/**
 * Given a set of clauses (as obtained by saturation)
 * turn them into formulas capturing the semantics of splitting assertions.
 *
 * Also, make the list duplicate free.
 */
UnitList* Splitter::explicateAssertionsForSaturatedClauseSet(UnitList* clauses)
{
  CALL("Splitter::explicateAssertionsForSaturatedClauseSet");

  DHMap<Clause*,Formula*> processed;
  DHMap<Clause*,Formula*> components;

  UnitList* result = UnitList::empty();

  UnitList::Iterator it(clauses);
  while (it.hasNext()) {
    Clause* cl = it.next()->asClause();

    // cout << "cl in: " << cl->toString() << endl;

    if (processed.find(cl)) { // removing duplicates
      continue;
    }

    Formula* f = Formula::fromClause(cl);

    if (cl->splits()) {
      FormulaList* disjuncts = FormulaList::empty();
      SplitSet::Iterator it(*cl->splits());
      while(it.hasNext()) {
        Clause* ass = getComponentClause(it.next());

        Formula** ass_f_p;

        if (components.getValuePtr(ass,ass_f_p)) {
          *ass_f_p = new NegatedFormula(Formula::fromClause(ass));
        }
        FormulaList::push(*ass_f_p,disjuncts);
      }
      if (FormulaList::isNonEmpty(disjuncts)) {
        FormulaList::push(f,disjuncts);

        f = JunctionFormula::generalJunction(OR, disjuncts);
      }
    }

    // cout << "fla out: " << f->toString() << endl;

    UnitList::push(new FormulaUnit(f,new Inference1(Inference::FORMULIFY,cl),
        // because units which are conjectures are explicitly negated in TPTPPrinter::toString for some reason:
        cl->inputType() == Unit::CONJECTURE ? Unit::NEGATED_CONJECTURE : cl->inputType()),
        result); // would be nice to preserve

    ALWAYS(processed.insert(cl,f));
  }

  return result;
}

}
