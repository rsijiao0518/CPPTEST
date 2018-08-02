
/*
 * File EqResWithDeletion.hpp.
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
 * @file EqResWithDeletion.hpp
 * Defines class EqResWithDeletion.
 */


#ifndef __Shell_EqResWithDeletion__
#define __Shell_EqResWithDeletion__

#include "Forwards.hpp"

#include "Lib/DHMap.hpp"

#include "Kernel/Term.hpp"

namespace Shell {

using namespace Lib;
using namespace Kernel;

class EqResWithDeletion
{
public:
  void apply(Problem& prb);
  bool apply(UnitList*& units);

  TermList apply(unsigned var);
  Clause* apply(Clause* cl);
private:
  bool scan(Literal* lit);



  /** The substitution induced by resolved inequalities
   * (It is reset with each clause). */
  DHMap<unsigned, TermList, IdentityHash> _subst;
};

};

#endif /* __Shell_EqResWithDeletion__ */
