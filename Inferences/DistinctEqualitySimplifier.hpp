
/*
 * File DistinctEqualitySimplifier.hpp.
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
 * @file DistinctEqualitySimplifier.hpp
 * Defines class DistinctEqualitySimplifier.
 */

#ifndef __DistinctEqualitySimplifier__
#define __DistinctEqualitySimplifier__

#include "Forwards.hpp"
#include "InferenceEngine.hpp"

namespace Inferences {

class DistinctEqualitySimplifier
: public ImmediateSimplificationEngine
{
public:
  CLASS_NAME(DistinctEqualitySimplifier);
  USE_ALLOCATOR(DistinctEqualitySimplifier);

  Clause* simplify(Clause* cl);
  static bool mustBeDistinct(TermList t1, TermList t2);
  static bool mustBeDistinct(TermList t1, TermList t2, unsigned& grp);
private:
  static bool canSimplify(Clause* cl);
};

}// namespace Inferences

#endif // INFERENCES_DISTINCTEQUALITYSIMPLIFIER_HPP_
