
/*
 * File EqualityResolution.hpp.
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
 * @file EqualityResolution.hpp
 * Defines class EqualityResolution.
 */


#ifndef __EqualityResolution__
#define __EqualityResolution__

#include "Forwards.hpp"

#include "InferenceEngine.hpp"

namespace Inferences {

using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

class EqualityResolution
: public GeneratingInferenceEngine
{
public:
  CLASS_NAME(EqualityResolution);
  USE_ALLOCATOR(EqualityResolution);

  ClauseIterator generateClauses(Clause* premise);
  static Clause* tryResolveEquality(Clause* cl, Literal* toResolve);
private:
  struct ResultFn;
  struct IsNegativeEqualityFn;

};


};// namespace Inferences

#endif // INFERENCES_EQUALITYRESOLUTION_HPP_
