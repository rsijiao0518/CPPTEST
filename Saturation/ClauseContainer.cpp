
/*
 * File ClauseContainer.cpp.
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
 * @file ClauseContainer.cpp
 * Implementing ClauseContainer and its descendants.
 */

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Environment.hpp"
#include "Lib/DHSet.hpp"
#include "Lib/Stack.hpp"
#include "Kernel/Clause.hpp"
#include "Shell/Statistics.hpp"

#include "Indexing/LiteralIndexingStructure.hpp"

#include "SaturationAlgorithm.hpp"

#if VDEBUG
#include <iostream>
using namespace std;
#endif

#include "ClauseContainer.hpp"

namespace Saturation
{

using namespace Kernel;
using namespace Indexing;

void ClauseContainer::addClauses(ClauseIterator cit)
{
  while (cit.hasNext()) {
    add(cit.next());
  }
}


/////////////////   RandomAccessClauseContainer   //////////////////////

void RandomAccessClauseContainer::removeClauses(ClauseIterator cit)
{
  while (cit.hasNext()) {
    remove(cit.next());
  }
}

/**
 * Attach to the SaturationAlgorithm object.
 *
 * This method is being called in the SaturationAlgorithm constructor,
 * so no virtual methods of SaturationAlgorithm should be called.
 */
void RandomAccessClauseContainer::attach(SaturationAlgorithm* salg)
{
  CALL("RandomAccessClauseContainer::attach");
  ASS(!_salg);

  _salg=salg;
  _limitChangeSData=_salg->getLimits()->changedEvent.subscribe(
      this, &PassiveClauseContainer::onLimitsUpdated);
}
/**
 * Detach from the SaturationAlgorithm object.
 *
 * This method is being called in the SaturationAlgorithm destructor,
 * so no virtual methods of SaturationAlgorithm should be called.
 */
void RandomAccessClauseContainer::detach()
{
  CALL("RandomAccessClauseContainer::detach");
  ASS(_salg);

  _limitChangeSData->unsubscribe();
  _salg=0;
}


/////////////////   UnprocessedClauseContainer   //////////////////////

UnprocessedClauseContainer::~UnprocessedClauseContainer()
{
  CALL("UnprocessedClauseContainer::~UnprocessedClauseContainer");

  while (!_data.isEmpty()) {
    Clause* cl=_data.pop_back();
    ASS_EQ(cl->store(), Clause::UNPROCESSED);
    cl->setStore(Clause::NONE);
  }
}

void UnprocessedClauseContainer::add(Clause* c)
{
  CALL("UnprocessedClauseContainer::add");

  _data.push_back(c);
  addedEvent.fire(c);
}

Clause* UnprocessedClauseContainer::pop()
{
  CALL("UnprocessedClauseContainer::pop");

  Clause* res=_data.pop_back();
  selectedEvent.fire(res);
  return res;
}



/////////////////   ActiveClauseContainer   //////////////////////

void ActiveClauseContainer::add(Clause* c)
{
  CALL("ActiveClauseContainer::add");

  _size++;

  ASS(c->store()==Clause::ACTIVE);
  if(!c->in_active()){
    addedEvent.fire(c);
    c->toggle_in_active();
  }
  ASS(c->in_active());
}

/**
 * Remove Clause from the Active store. Should be called only
 * when the Clause is no longer needed by the inference process
 * (i.e. was backward subsumed/simplified), as it can result in
 * deletion of the clause.
 */
void ActiveClauseContainer::remove(Clause* c)
{
  ASS(c->store()==Clause::ACTIVE);
  ASS(c->in_active());

  _size--;
  removedEvent.fire(c);

  c->toggle_in_active();
  ASS(!c->in_active());

} // Active::ClauseContainer::remove

void ActiveClauseContainer::onLimitsUpdated(LimitsChangeType change)
{
  CALL("ActiveClauseContainer::onLimitsUpdated");

  if (change==LIMITS_LOOSENED) {
    return;
  }
  LiteralIndexingStructure* gis=getSaturationAlgorithm()->getIndexManager()
      ->getGeneratingLiteralIndexingStructure();
  if (!gis) {
    return;
  }
  Limits* limits=getSaturationAlgorithm()->getLimits();

  if (!limits->ageLimited() || !limits->weightLimited()) {
    return;
  }
  unsigned ageLimit=limits->ageLimit();
  unsigned weightLimit=limits->weightLimit();

  static DHSet<Clause*> checked;
  static Stack<Clause*> toRemove(64);
  checked.reset();
  toRemove.reset();

  SLQueryResultIterator rit=gis->getAll();
  while (rit.hasNext()) {
    Clause* cl=rit.next().clause;
    if (cl->age()<ageLimit || checked.contains(cl)) {
      continue;
    }
    checked.insert(cl);

    bool shouldRemove;
    if (cl->age()>ageLimit) {
      shouldRemove=cl->getEffectiveWeight(_opt)>weightLimit;
    }
    else {
      unsigned selCnt=cl->numSelected();
      unsigned maxSelWeight=0;
      for(unsigned i=0;i<selCnt;i++) {
        maxSelWeight=max((*cl)[i]->weight(),maxSelWeight);
      }
      // This weight() value now includes splitWeights (if the appropriate option is set, 
      //                                                which it didn't before)
      // Makes sense as weight() is used like this elsewhere to reflect whether
      // the clause is used.
      // Note that the idea behind this branch seems to be that we want to check if
      //  the clause is still too heavy without the heaviest selected literal.
      //  Increasing weight() will make this more likely, increasing the liklihood of
      //  highly dependent clauses being discarded (in LRS)
      shouldRemove=cl->weight()-(int)maxSelWeight>=weightLimit;
    }

    if (shouldRemove) {
      ASS(cl->store()==Clause::ACTIVE);
      toRemove.push(cl);
    }
  }

#if OUTPUT_LRS_DETAILS
  if (toRemove.isNonEmpty()) {
    cout<<toRemove.size()<<" active deleted\n";
  }
#endif

  while (toRemove.isNonEmpty()) {
    Clause* removed=toRemove.pop();
    ASS(removed->store()==Clause::ACTIVE);

    RSTAT_CTR_INC("clauses discarded from active on weight limit update");
    env.statistics->discardedNonRedundantClauses++;

    remove(removed);
    ASS_NEQ(removed->store(), Clause::ACTIVE);
  }
}

}

