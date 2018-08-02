
/*
 * File ExtensionalityClauseContainer.cpp.
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
#include "Kernel/Clause.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/Term.hpp"

#include "Shell/Statistics.hpp"
#include "Shell/TheoryFinder.hpp"

#include "Saturation/ExtensionalityClauseContainer.hpp"

namespace Saturation
{

using namespace Shell;

/**
 * Check if clause is considered as an extensionality clause (according to
 * options). If so, track in extensionality container for extensionality
 * resolution inferences.
 *
 * Common to all extensionality clauses is a single positive variable equality
 * X=Y, which is returned in case of a positive match, 0 otherwise.
 */
Literal* ExtensionalityClauseContainer::addIfExtensionality(Clause* c) {
  CALL("ExtensionalityClauseContainer::addIfExtensionality");
  
  // Clause is already in extensionality container. We only have to search X=Y.
  if (c->isExtensionality()) {
    //cout << "Using " << c->toString() << endl;
    return getSingleVarEq(c);
  }

  // We only consider extensionality clauses of at least length 2, but can also
  // specify a length limit.
  unsigned clen = c->length();
  if (clen < 2 || (_maxLen > 1 && clen > _maxLen))
    return 0;

  Literal* varEq = 0;
  unsigned sort;

  if (_onlyKnown) {
    // We only match agains specific extensionality axiom patterns (e.g. set,
    // array, ...).
    if(!TheoryFinder::matchKnownExtensionality(c))
      return 0;

    // We know that the patterns only have a single X=Y.
    varEq = getSingleVarEq(c);
    sort = varEq->twoVarEqSort();
  } else if (!_onlyTagged || c->isTaggedExtensionality()) {
    // Generic filter for extensionality clauses.
    //   * Exactly one X=Y
    //   * No inequality of same sort as X=Y
    //   * No equality except X=Y (optional).
    static DArray<bool> negEqSorts(_sortCnt);
    negEqSorts.init(_sortCnt, false);
  
    for (Clause::Iterator ci(*c); ci.hasNext(); ) {
      Literal* l = ci.next();

      if (l->isTwoVarEquality() && l->isPositive()) {
        if (varEq != 0)
          return 0;

        sort = l->twoVarEqSort();
        if (negEqSorts[sort])
          return 0;

        varEq = l;
      } else if (l->isEquality()) {
        if (!_allowPosEq && l->isPositive())
          return 0;
      
        unsigned negEqSort = SortHelper::getEqualityArgumentSort(l);
        if (varEq == 0)
          negEqSorts[negEqSort] = true;
        else if (sort == negEqSort)
          return 0;
      }
    }
  }

  if (varEq != 0) {
    // If varEq is nonzero then sort must have been set above.
    c->setExtensionality(true);
    add(ExtensionalityClause(c, varEq, sort));
    _size++;
    env.statistics->extensionalityClauses++;
    return varEq;
  }

  return 0;
}

/**
 * Get the single positive variable equality of an extensionality clause.
 * Actually returns the first such equality and hence should be used only in
 * places where we already know that @c c is an extensionality clause.
 */
Literal* ExtensionalityClauseContainer::getSingleVarEq(Clause* c) {
  CALL("ExtensionalityClauseContainer::getSingleVarEq");
  
  for (unsigned i = 0; i < c->length(); ++i) {
    Literal* varEq = (*c)[i];
    if (varEq->isTwoVarEquality() && varEq->isPositive()) {
      return varEq;
      break;
    }
  }
  ASSERTION_VIOLATION;
}

void ExtensionalityClauseContainer::add(ExtensionalityClause c) {
  CALL("ExtensionalityClauseContainer::add");
  
  ExtensionalityClauseList::push(c, _clausesBySort[c.sort]);
}

/**
 * Functor for lazily removing deleted extensionality clauses from the container.
 * See activeIterator(unsigned sort).
 */
struct ExtensionalityClauseContainer::ActiveFilterFn
{
  ActiveFilterFn(ExtensionalityClauseContainer& parent) : _parent(parent) {}
  DECL_RETURN_TYPE(bool);
  OWN_RETURN_TYPE operator()(ExtensionalityClause extCl)
  {
    CALL("ExtensionalityClauseContainer::ActiveFilterFn::operator()");
    
    if (extCl.clause->store() != Clause::ACTIVE) {
      extCl.clause->setExtensionality(false);
      _parent._size--;
      return false;
    }
    return true;
  }
private:
  ExtensionalityClauseContainer& _parent;
};

/**
 * Returns an iterator over the active extensionality clauses of a particular @c
 * sort. Nonactive clauses in the container are removed during iteration.
 * 
 * In other words, if an extensionality clause gets deleted from the search
 * space, we do not immediately remove it from the container. Instead we check
 * this lazily during generating inferences.
 */
ExtensionalityClauseIterator ExtensionalityClauseContainer::activeIterator(unsigned sort) {
  CALL("ExtensionalityClauseContainer::activeIterator");
  
  return pvi(getFilteredDelIterator(
               ExtensionalityClauseList::DelIterator(_clausesBySort[sort]),
               ActiveFilterFn(*this)));
}

void ExtensionalityClauseContainer::print (ostream& out) {
  CALL("ExtensionalityClauseContainer::print");
  
  out << "#####################" << endl;

  for(size_t i = 0; i < _clausesBySort.size(); ++i) {
    ExtensionalityClauseList::Iterator it(_clausesBySort[i]);
    while(it.hasNext()) {
      ExtensionalityClause c = it.next();
      out << c.clause->toString() << endl
          << c.literal->toString() << endl
          << c.sort << endl;
    }
  }
  
  out << "#####################" << endl;
}

}
