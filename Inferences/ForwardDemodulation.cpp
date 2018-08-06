
/*
 * File ForwardDemodulation.cpp.
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
 * @file ForwardDemodulation.cpp
 * Implements class ForwardDemodulation.
 */
#include <algorithm>

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/DHSet.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/TimeCounter.hpp"
#include "Lib/Timer.hpp"
#include "Lib/VirtualIterator.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Ordering.hpp"
#include "Kernel/Renaming.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/ColorHelper.hpp"

#include "Indexing/Index.hpp"
#include "Indexing/IndexManager.hpp"
#include "Indexing/TermIndex.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include "ForwardDemodulation.hpp"

namespace Inferences {

using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

void ForwardDemodulation::attach(SaturationAlgorithm* salg)
{
  CALL("ForwardDemodulation::attach");
  ForwardSimplificationEngine::attach(salg);
  _index=static_cast<DemodulationLHSIndex*>(
	  _salg->getIndexManager()->request(DEMODULATION_LHS_SUBST_TREE) );

  _preorderedOnly=getOptions().forwardDemodulation()==Options::Demodulation::PREORDERED;
}

void ForwardDemodulation::detach()
{
  CALL("ForwardDemodulation::detach");
  _index=0;
  _salg->getIndexManager()->release(DEMODULATION_LHS_SUBST_TREE);
  ForwardSimplificationEngine::detach();
}


bool ForwardDemodulation::perform(Clause* cl, Clause*& replacement, ClauseIterator& premises)
{
  CALL("ForwardDemodulation::perform");

  TimeCounter tc(TC_FORWARD_DEMODULATION);

  Ordering& ordering = _salg->getOrdering();

  //Perhaps it might be a good idea to try to
  //replace subterms in some special order, like
  //the heaviest first...

  static DHSet<TermList> attempted;
  attempted.reset();

  unsigned cLen=cl->length();
  for(unsigned li=0;li<cLen;li++) {
    Literal* lit=(*cl)[li];
    NonVariableIterator nvi(lit);
    while(nvi.hasNext()) {
      TermList trm=nvi.next();
      if(!attempted.insert(trm)) {
	//We have already tried to demodulate the term @b trm and did not
	//succeed (otherwise we would have returned from the function).
	//If we have tried the term @b trm, we must have tried to
	//demodulate also its subterms, so we can skip them too.
	nvi.right();
	continue;
      }

      unsigned querySort = SortHelper::getTermSort(trm, lit);

      bool toplevelCheck=getOptions().demodulationRedundancyCheck() && lit->isEquality() &&
	  (trm==*lit->nthArgument(0) || trm==*lit->nthArgument(1));

      TermQueryResultIterator git=_index->getGeneralizations(trm, true);
      while(git.hasNext()) {
	TermQueryResult qr=git.next();
	ASS_EQ(qr.clause->length(),1);

	if(!ColorHelper::compatible(cl->color(), qr.clause->color())) {
	  continue;
	}

	unsigned eqSort = SortHelper::getEqualityArgumentSort(qr.literal);

	if(querySort!=eqSort) {
	  continue;
	}

	TermList rhs=EqHelper::getOtherEqualitySide(qr.literal,qr.term);
	TermList rhsS;
	if(!qr.substitution->isIdentityOnQueryWhenResultBound()) {
	  //When we apply substitution to the rhs, we get a term, that is
	  //a variant of the term we'd like to get, as new variables are
	  //produced in the substitution application.
	  TermList lhsSBadVars=qr.substitution->applyToResult(qr.term);
	  TermList rhsSBadVars=qr.substitution->applyToResult(rhs);
	  Renaming rNorm, qNorm, qDenorm;
	  rNorm.normalizeVariables(lhsSBadVars);
	  qNorm.normalizeVariables(trm);
	  qDenorm.makeInverse(qNorm);
	  ASS_EQ(trm,qDenorm.apply(rNorm.apply(lhsSBadVars)));
	  rhsS=qDenorm.apply(rNorm.apply(rhsSBadVars));
	} else {
	  rhsS=qr.substitution->applyToBoundResult(rhs);
	}

	Ordering::Result argOrder = ordering.getEqualityArgumentOrder(qr.literal);
	bool preordered = argOrder==Ordering::LESS || argOrder==Ordering::GREATER;
#if VDEBUG
	if(preordered) {
	  if(argOrder==Ordering::LESS) {
	    ASS_EQ(rhs, *qr.literal->nthArgument(0));
	  }
	  else {
	    ASS_EQ(rhs, *qr.literal->nthArgument(1));
	  }
	}
#endif
	if(!preordered && (_preorderedOnly || ordering.compare(trm,rhsS)!=Ordering::GREATER) ) {
	  continue;
	}

	if(toplevelCheck) {
	  TermList other=EqHelper::getOtherEqualitySide(lit, trm);
	  Ordering::Result tord=ordering.compare(rhsS, other);
	  if(tord!=Ordering::LESS && tord!=Ordering::LESS_EQ) {
	    Literal* eqLitS=qr.substitution->applyToBoundResult(qr.literal);
	    bool isMax=true;
	    for(unsigned li2=0;li2<cLen;li2++) {
	      if(li==li2) {
		continue;
	      }
	      if(ordering.compare(eqLitS, (*cl)[li2])==Ordering::LESS) {
		isMax=false;
		break;
	      }
	    }
	    if(isMax) {
	      //RSTAT_CTR_INC("tlCheck prevented");
	      //The demodulation is this case which doesn't preserve completeness:
	      //s = t     s = t1 \/ C
	      //---------------------
	      //     t = t1 \/ C
	      //where t > t1 and s = t > C
	      continue;
	    }
	  }
	}

	Literal* resLit = EqHelper::replace(lit,trm,rhsS);
	if(EqHelper::isEqTautology(resLit)) {
	  env.statistics->forwardDemodulationsToEqTaut++;
	  premises = pvi( getSingletonIterator(qr.clause));
	  return true;
	}

	Inference* inf = new Inference2(Inference::FORWARD_DEMODULATION, cl, qr.clause);
	Unit::InputType inpType = (Unit::InputType)
		Int::max(cl->inputType(), qr.clause->inputType());

	Clause* res = new(cLen) Clause(cLen, inpType, inf);

	(*res)[0]=resLit;

	unsigned next=1;
	for(unsigned i=0;i<cLen;i++) {
	  Literal* curr=(*cl)[i];
	  if(curr!=lit) {
	    (*res)[next++] = curr;
	  }
	}
	ASS_EQ(next,cLen);

	res->setAge(cl->age());
	env.statistics->forwardDemodulations++;

	premises = pvi( getSingletonIterator(qr.clause));
	replacement = res;
	return true;

      }
    }
  }

  return false;
}

}// namespace Inferences
