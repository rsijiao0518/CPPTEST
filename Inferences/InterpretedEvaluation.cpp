
/*
 * File InterpretedEvaluation.cpp.
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
 * @file InterpretedEvaluation.cpp
 * Implements class InterpretedEvaluation.
 */

#include "Lib/Exception.hpp"
#include "Lib/DArray.hpp"
#include "Lib/Stack.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/Int.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Signature.hpp"

#include "Indexing/TermSharing.hpp"

#include "Shell/Statistics.hpp"

#include "InterpretedEvaluation.hpp"

#undef LOGGING
#define LOGGING 0

namespace Inferences
{
using namespace Lib;
using namespace Kernel;


InterpretedEvaluation::InterpretedEvaluation()
{
  CALL("InterpretedEvaluation::InterpretedEvaluation");

  _simpl = new InterpretedLiteralEvaluator();
}

InterpretedEvaluation::~InterpretedEvaluation()
{
  CALL("InterpretedEvaluation::~InterpretedEvaluation");

  delete _simpl;
}



bool InterpretedEvaluation::simplifyLiteral(Literal* lit,
	bool& constant, Literal*& res, bool& constantTrue,Stack<Literal*>& sideConditions)
{
  CALL("InterpretedEvaluation::evaluateLiteral");

  if(lit->arity()==0 || !lit->hasInterpretedConstants()) {
    //we have no interpreted predicates of zero arity
    return false;
  }

  bool okay = _simpl->evaluate(lit, constant, res, constantTrue,sideConditions);

  //if(okay && lit!=res){
  //  cout << "evaluate " << lit->toString() << " to " << res->toString() << endl;
  //}

  return okay;
}

Clause* InterpretedEvaluation::simplify(Clause* cl)
{
  CALL("InterpretedEvaluation::perform");

  TimeCounter tc(TC_INTERPRETED_EVALUATION);

  // do not evaluate theory axioms
  if(cl->inference()->rule()==Inference::THEORY) return cl;

  static DArray<Literal*> newLits(32);
  unsigned clen=cl->length();
  bool modified=false;
  newLits.ensure(clen);
  unsigned next=0;
  Stack<Literal*> sideConditions;
  for(unsigned li=0;li<clen; li++) {
    Literal* lit=(*cl)[li];
    Literal* res;
    bool constant, constTrue;
    bool litMod=simplifyLiteral(lit, constant, res, constTrue,sideConditions);
    if(!litMod) {
      newLits[next++]=lit;
      continue;
    }
    modified=true;
    if(constant) {
      if(constTrue) {
        //cout << "evaluate " << cl->toString() << " to true" << endl;
	env.statistics->evaluations++;
	return 0;
      } else {
	continue;
      }
    }
    newLits[next++]=res;
  }
  if(!modified) {
    return cl;
  }

  Stack<Literal*>::Iterator side(sideConditions);
  newLits.expand(clen+sideConditions.length());
  while(side.hasNext()){ newLits[next++]=side.next();}
  int newLength = next;
  Inference* inf = new Inference1(Inference::EVALUATION, cl);
  Unit::InputType inpType = cl->inputType();
  Clause* res = new(newLength) Clause(newLength, inpType, inf);

  for(int i=0;i<newLength;i++) {
    (*res)[i] = newLits[i];
  }

  res->setAge(cl->age());
  env.statistics->evaluations++;

  //cout << "evaluated " << cl->toString() << " to " << res->toString() << endl;

  return res;
}

}// namespace Inferences
