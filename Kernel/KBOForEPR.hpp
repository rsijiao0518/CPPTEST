
/*
 * File KBOForEPR.hpp.
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
 * @file KBOForEPR.hpp
 * Defines class KBOForEPR for instances of the Knuth-Bendix ordering for EPR problems
 */

#ifndef __KBOForEPR__
#define __KBOForEPR__

#include "Forwards.hpp"

#include "KBO.hpp"

namespace Kernel {

using namespace Lib;

/**
 * Class for instances of the Knuth-Bendix orderings
 * @since 30/04/2008 flight Brussels-Tel Aviv
 */
class KBOForEPR
: public PrecedenceOrdering
{
public:
  CLASS_NAME(KBOForEPR);
  USE_ALLOCATOR(KBOForEPR);

  KBOForEPR(Problem& prb, const Options& opt);

  using PrecedenceOrdering::compare;
  Result compare(TermList tl1, TermList tl2) const override;
protected:
  Result comparePredicates(Literal* l1, Literal* l2) const override;
};

}
#endif
