
/*
 * File ModelPrinter.hpp.
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
 * @file ModelPrinter.hpp
 * Defines class ModelPrinter.
 */

#ifndef __ModelPrinter__
#define __ModelPrinter__

#include "Forwards.hpp"

#include "Lib/DHMap.hpp"
#include "Lib/Stack.hpp"

#include "Kernel/Term.hpp"


namespace InstGen {

using namespace Kernel;
using namespace SAT;

class ModelPrinter {
public:
  explicit ModelPrinter(IGAlgorithm& iga);

  bool tryOutput(ostream& stm);
private:

  struct InstLitComparator;
  struct PredNumComparator;

  bool haveNonDefaultSorts();
  static bool isEprProblem();
  static bool isEquality(Literal* lit);

  void collectTrueLits();
  void getInstances(LiteralStack& trueLits, TermStack& domain, LiteralStack& instanceAcc);
  void generateNewInstances(Literal* base, TermStack& domain, DHSet<Literal*>& instSet, LiteralStack& instAcc);
  void analyzeEqualityAndPopulateDomain();
  void rewriteLits(LiteralStack& lits);

  void outputDomainSpec(ostream& out);
  void outputFunInterpretations(ostream& out);
  void outputPredInterpretations(ostream& out);

  void collectConstants(Literal* lit);

  DHSet<unsigned> _usedConstantSet;
  TermStack _usedConstants;

  Stack<TermList> _domain;


  typedef DHMap<TermList,TermList> EqMap;
  /** Maps terms to representants of their equivalence classes */
  EqMap _rewrites;

  LiteralStack _trueLits;
  LiteralStack _trueEqs;

  IGAlgorithm& _iga;
};

}// namespace InstGen

#endif // INSTGEN_MODELPRINTER_HPP_
