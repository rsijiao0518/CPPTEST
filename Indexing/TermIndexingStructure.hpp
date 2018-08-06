
/*
 * File TermIndexingStructure.hpp.
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
 * @file TermIndexingStructure.hpp
 * Defines class TermIndexingStructure.
 */


#ifndef __TermIndexingStructure__
#define __TermIndexingStructure__

#include "Index.hpp"

namespace Indexing {

class TermIndexingStructure {
public:
  virtual ~TermIndexingStructure() {}

  virtual void insert(TermList t, Literal* lit, Clause* cls) = 0;
  virtual void remove(TermList t, Literal* lit, Clause* cls) = 0;

  virtual TermQueryResultIterator getUnifications(TermList t,
	  bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }
  virtual TermQueryResultIterator getUnificationsWithConstraints(TermList t,
          bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }
  virtual TermQueryResultIterator getGeneralizations(TermList t,
	  bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }
  virtual TermQueryResultIterator getInstances(TermList t,
	  bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }

  virtual bool generalizationExists(TermList t) { NOT_IMPLEMENTED; }

#if VDEBUG
  virtual void markTagged() = 0;
#endif

};

};// namespace Indexing

#endif // INDEXING_TERMINDEXINGSTRUCTURE_HPP_
