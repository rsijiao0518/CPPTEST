
/*
 * File TermIndex.hpp.
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
 * @file TermIndex.hpp
 * Defines class TermIndex.
 */


#ifndef __TermIndex__
#define __TermIndex__

#include "Index.hpp"

namespace Indexing {

class TermIndex
: public Index
{
public:
  CLASS_NAME(TermIndex);
  USE_ALLOCATOR(TermIndex);

  virtual ~TermIndex();

  TermQueryResultIterator getUnifications(TermList t,
	  bool retrieveSubstitutions = true);
  TermQueryResultIterator getUnificationsWithConstraints(TermList t,
          bool retrieveSubstitutions = true);
  TermQueryResultIterator getGeneralizations(TermList t,
	  bool retrieveSubstitutions = true);
  TermQueryResultIterator getInstances(TermList t,
	  bool retrieveSubstitutions = true);

protected:
  explicit TermIndex(TermIndexingStructure* is) : _is(is) {}

  TermIndexingStructure* _is;
};

class SuperpositionSubtermIndex
: public TermIndex
{
public:
  CLASS_NAME(SuperpositionSubtermIndex);
  USE_ALLOCATOR(SuperpositionSubtermIndex);

  SuperpositionSubtermIndex(TermIndexingStructure* is, Ordering& ord)
  : TermIndex(is), _ord(ord) {};
protected:
  void handleClause(Clause* c, bool adding);
private:
  Ordering& _ord;
};

class SuperpositionLHSIndex
: public TermIndex
{
public:
  CLASS_NAME(SuperpositionLHSIndex);
  USE_ALLOCATOR(SuperpositionLHSIndex);

  SuperpositionLHSIndex(TermIndexingStructure* is, Ordering& ord, const Options& opt)
  : TermIndex(is), _ord(ord), _opt(opt) {};
protected:
  void handleClause(Clause* c, bool adding);
private:
  Ordering& _ord;
  const Options& _opt;
};

/**
 * Term index for backward demodulation
 */
class DemodulationSubtermIndex
: public TermIndex
{
public:
  CLASS_NAME(DemodulationSubtermIndex);
  USE_ALLOCATOR(DemodulationSubtermIndex);

  explicit DemodulationSubtermIndex(TermIndexingStructure* is)
  : TermIndex(is) {};
protected:
  void handleClause(Clause* c, bool adding);
};

/**
 * Term index for forward demodulation
 */
class DemodulationLHSIndex
: public TermIndex
{
public:
  CLASS_NAME(DemodulationLHSIndex);
  USE_ALLOCATOR(DemodulationLHSIndex);

  DemodulationLHSIndex(TermIndexingStructure* is, Ordering& ord, const Options& opt)
  : TermIndex(is), _ord(ord), _opt(opt) {};
protected:
  void handleClause(Clause* c, bool adding);
private:
  Ordering& _ord;
  const Options& _opt;
};

};// namespace Indexing
#endif // INDEXING_TERMINDEX_HPP_
