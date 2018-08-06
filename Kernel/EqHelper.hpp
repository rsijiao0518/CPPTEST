
/*
 * File EqHelper.hpp.
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
 * @file EqHelper.hpp
 * Defines class EqHelper.
 */


#ifndef __EqHelper__
#define __EqHelper__

#include "Forwards.hpp"

#include "Lib/VirtualIterator.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/PairUtils.hpp"

#include "Term.hpp"

namespace Kernel {

using namespace Lib;
using namespace Shell;

class EqHelper
{
public:
  static TermList getOtherEqualitySide(Literal* eq, TermList lhs);
  static bool hasGreaterEqualitySide(Literal* eq, const Ordering& ord, TermList& lhs, TermList& rhs);
  static TermIterator getRewritableSubtermIterator(Literal* lit, const Ordering& ord);
  static TermIterator getLHSIterator(Literal* lit, const Ordering& ord);
  static TermIterator getSuperpositionLHSIterator(Literal* lit, const Ordering& ord, const Options& opt);
  static TermIterator getDemodulationLHSIterator(Literal* lit, bool forward, const Ordering& ord, const Options& opt);
  static TermIterator getEqualityArgumentIterator(Literal* lit);

  static Term* replace(Term* t, TermList what, TermList by);
  static Literal* replace(Literal* lit, TermList what, TermList by);

  struct LHSIteratorFn
  {
    LHSIteratorFn(const Ordering& ord) : _ord(ord) {}

    DECL_RETURN_TYPE(VirtualIterator<pair<Literal*, TermList> >);
    OWN_RETURN_TYPE operator()(Literal* lit)
    {
      return pvi( pushPairIntoRightIterator(lit, getLHSIterator(lit, _ord)) );
    }
  private:
    const Ordering& _ord;
  };

  struct SuperpositionLHSIteratorFn
  {
    SuperpositionLHSIteratorFn(const Ordering& ord, const Options& opt) : _ord(ord), _opt(opt) {}

    DECL_RETURN_TYPE(VirtualIterator<pair<Literal*, TermList> >);
    OWN_RETURN_TYPE operator()(Literal* lit)
    {
      return pvi( pushPairIntoRightIterator(lit, getSuperpositionLHSIterator(lit, _ord, _opt)) );
    }
  private:
    const Ordering& _ord;
    const Options& _opt;
  };

  struct EqualityArgumentIteratorFn
  {
    DECL_RETURN_TYPE(VirtualIterator<pair<Literal*, TermList> >);
    OWN_RETURN_TYPE operator()(Literal* lit)
    {
      return pvi( pushPairIntoRightIterator(lit, getEqualityArgumentIterator(lit)) );
    }
  };

  static bool isEqTautology(Literal* lit)
  {
    return lit->isEquality() && lit->isPositive() && (*lit->nthArgument(0))==(*lit->nthArgument(1));
  }
private:

  struct IsNonVariable;

};

};

#endif /* __EqHelper__ */
