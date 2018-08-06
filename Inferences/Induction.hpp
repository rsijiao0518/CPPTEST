/*
 * File Induction 
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
 * @file Induction.hpp
 * Defines class Induction
 *
 */

#ifndef __Induction__
#define __Induction__

#include "Forwards.hpp"

#include "Kernel/TermTransformer.hpp"

#include "InferenceEngine.hpp"

namespace Inferences
{

using namespace Kernel;
using namespace Saturation;

class TermReplacement : public TermTransformer {

public:
  TermReplacement(Term* o, TermList r) : _o(o), _r(r) {} 
  virtual TermList transformSubterm(TermList trm);
private:
  Term* _o;
  TermList _r;
};

class Induction
: public GeneratingInferenceEngine
{
public:
  CLASS_NAME(Induction);
  USE_ALLOCATOR(Induction);

  Induction() {}
  ClauseIterator generateClauses(Clause* premise);

};

class InductionClauseIterator
{
public:
  // all the work happens in the constructor!
  explicit InductionClauseIterator(Clause* premise);

  CLASS_NAME(InductionClauseIterator);
  USE_ALLOCATOR(InductionClauseIterator);
  DECL_ELEMENT_TYPE(Clause*);

  inline bool hasNext() { return _clauses.isNonEmpty(); }
  inline OWN_ELEMENT_TYPE next() { 
    Clause* c = _clauses.pop();
    c->incInductionDepth();
    return c; 
  }

private:
  void process(Clause* premise, Literal* lit);

  void performMathInductionOne(Clause* premise, Literal* lit, Term* t); 
  void performMathInductionTwo(Clause* premise, Literal* lit, Term* t);

  void performStructInductionOne(Clause* premise, Literal* lit, Term* t);
  void performStructInductionTwo(Clause* premise, Literal* lit, Term* t);
  void performStructInductionThree(Clause* premise, Literal* lit, Term* t);

  bool notDone(Literal* lit, Term* t);

  Stack<Clause*> _clauses;
};

};// namespace Inferences

#endif // INFERENCES_INDUCTION_HPP_
