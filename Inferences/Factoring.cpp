
/*
 * File Factoring.cpp.
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
 * @file Factoring.cpp
 * Implements class Factoring.
 */

#include <utility>

#include "Lib/Comparison.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/PairUtils.hpp"
#include "Lib/VirtualIterator.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/LiteralSelector.hpp"
#include "Kernel/RobSubstitution.hpp"
#include "Kernel/Ordering.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Statistics.hpp"

#include "Factoring.hpp"

namespace Inferences
{

using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Saturation;


/**
 * This functor, given pair of unsigned integers,
 * returns iterator of pairs of unification of literals at
 * positions corresponding to these integers and one
 * of these literals. (Literal stays the same and unifiers vary.)
 */
class Factoring::UnificationsOnPositiveFn
{
public:
  DECL_RETURN_TYPE(VirtualIterator<pair<Literal*,RobSubstitution*> >);
  UnificationsOnPositiveFn(Clause* cl, LiteralSelector& sel)
  : _cl(cl), _sel(sel)
  {
    _subst=RobSubstitutionSP(new RobSubstitution());
  }
  OWN_RETURN_TYPE operator() (pair<unsigned,unsigned> nums)
  {
    CALL("Factoring::UnificationsFn::operator()");

    Literal* l1 = (*_cl)[nums.first];
    Literal* l2 = (*_cl)[nums.second];

    //we assume there are no duplicate literals
    ASS(l1!=l2);
    ASS_EQ(_subst->size(),0);

    if(l1->isEquality()) {
      //We don't perform factoring with equalities
      return OWN_RETURN_TYPE::getEmpty();
    }

    if(_sel.isNegativeForSelection(l1)) {
      //We don't perform factoring on negative literals
      // (this check only becomes relevant, when there is more than one literal selected
      // and yet the selected ones are not all positive -- see the check in generateClauses)
      return OWN_RETURN_TYPE::getEmpty();
    }

    SubstIterator unifs=_subst->unifiers(l1,0,l2,0, false);
    if(!unifs.hasNext()) {
      return OWN_RETURN_TYPE::getEmpty();
    }

    return pvi( pushPairIntoRightIterator(l2, unifs) );
  }
private:
  Clause* _cl;
  LiteralSelector& _sel;
  RobSubstitutionSP _subst;
};

/**
 * This functor given a pair of a literal and a substitution,
 * removes the literal from the clause specified in constructor,
 * applies the substitution, and returns resulting clause.
 * (Also it records this to statistics as factoring.)
 */
class Factoring::ResultsFn
{
public:
  DECL_RETURN_TYPE(Clause*);
  ResultsFn(Clause* cl, bool afterCheck, Ordering& ord)
  : _cl(cl), _cLen(cl->length()), _afterCheck(afterCheck), _ord(ord) {}
  OWN_RETURN_TYPE operator() (pair<Literal*,RobSubstitution*> arg)
  {
    CALL("Factoring::ResultsFn::operator()");

    unsigned newLength = _cLen-1;
    Inference* inf = new Inference1(Inference::FACTORING, _cl);
    Clause* res = new(newLength) Clause(newLength, _cl->inputType(), inf);

    unsigned next = 0;
    Literal* skipped=arg.first;

    Literal* skippedAfter = 0;
    if (_afterCheck && _cl->numSelected() > 1) {
      TimeCounter tc(TC_LITERAL_ORDER_AFTERCHECK);

      skippedAfter = arg.second->apply(skipped, 0);
    }

    for(unsigned i=0;i<_cLen;i++) {
      Literal* curr=(*_cl)[i];
      if(curr!=skipped) {
        Literal* currAfter = arg.second->apply(curr, 0);

        if (skippedAfter) {
          TimeCounter tc(TC_LITERAL_ORDER_AFTERCHECK);

          if (i < _cl->numSelected() && _ord.compare(currAfter,skippedAfter) == Ordering::GREATER) {
            env.statistics->inferencesBlockedForOrderingAftercheck++;
            res->destroy();
            return 0;
          }
        }

        (*res)[next++] = currAfter;
      }
    }
    ASS_EQ(next,newLength);

    res->setAge(_cl->age()+1);
    env.statistics->factoring++;

    return res;
  }
private:
  Clause* _cl;
  ///length of the premise clause
  unsigned _cLen;
  bool _afterCheck;
  Ordering& _ord;
};

/**
 * Return ClauseIterator, that yields clauses generated from
 * @b premise by the factoring inference rule.
 *
 * Nothing is generated, when the premise contains only one
 * negative literal. Otherwise one of literals used in factoring
 * has to be selected, the other one does not. This deviation from
 * usual factoring rules, where both factored literals have to be
 * selected, is for the sake of incomplete literal selection
 * functions, that select always just one literal. (This would lead
 * to no factoring at all.)
 *
 * If a complete literal selection is used, this makes no difference,
 * as when two literals are unifiable, one cannot be maximal and the
 * other non-maximal in the literal ordering.
 */
ClauseIterator Factoring::generateClauses(Clause* premise)
{
  CALL("Factoring::generateClauses");

  if(premise->length()<=1) {
    return ClauseIterator::getEmpty();
  }
  if(premise->numSelected()==1 && _salg->getLiteralSelector().isNegativeForSelection((*premise)[0])) {
    return ClauseIterator::getEmpty();
  }

  auto it1 = getCombinationIterator(0u,premise->numSelected(),premise->length());

  auto it2 = getMapAndFlattenIterator(it1,UnificationsOnPositiveFn(premise,_salg->getLiteralSelector()));

  auto it3 = getMappingIterator(it2,ResultsFn(premise,
      getOptions().literalMaximalityAftercheck() && _salg->getLiteralSelector().isBGComplete(),_salg->getOrdering()));

  auto it4 = getFilteredIterator(it3, NonzeroFn());

  return pvi( it4 );
}

}// namespace Inferences
