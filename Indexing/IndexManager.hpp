
/*
 * File IndexManager.hpp.
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
 * @file IndexManager.hpp
 * Defines class IndexManager
 *
 */

#ifndef __IndexManager__
#define __IndexManager__

#include "Forwards.hpp"
#include "Lib/DHMap.hpp"
#include "Index.hpp"

#include "Lib/Allocator.hpp"


namespace Indexing
{

using namespace Lib;
using namespace Saturation;

enum IndexType {
  GENERATING_SUBST_TREE=1,
  SIMPLIFYING_SUBST_TREE,
  SIMPLIFYING_UNIT_CLAUSE_SUBST_TREE,
  GENERATING_UNIT_CLAUSE_SUBST_TREE,
  GENERATING_NON_UNIT_CLAUSE_SUBST_TREE,
  SUPERPOSITION_SUBTERM_SUBST_TREE,
  SUPERPOSITION_LHS_SUBST_TREE,
  DEMODULATION_SUBTERM_SUBST_TREE,
  DEMODULATION_LHS_SUBST_TREE,

  FW_SUBSUMPTION_CODE_TREE,

  FW_SUBSUMPTION_SUBST_TREE,
  BW_SUBSUMPTION_SUBST_TREE,

  REWRITE_RULE_SUBST_TREE,

  GLOBAL_SUBSUMPTION_INDEX,

  ACYCLICITY_INDEX
//  ARITHMETIC_INDEX
};

class IndexManager
{
public:
  CLASS_NAME(IndexManager);
  USE_ALLOCATOR(IndexManager);

  /** alg can be zero, then it must be set by setSaturationAlgorithm */
  explicit IndexManager(SaturationAlgorithm* alg);
  ~IndexManager();
  void setSaturationAlgorithm(SaturationAlgorithm* alg);
  Index* request(IndexType t);
  void release(IndexType t);
  bool contains(IndexType t);
  Index* get(IndexType t);

  void provideIndex(IndexType t, Index* index);

  LiteralIndexingStructure* getGeneratingLiteralIndexingStructure() { ASS(_genLitIndex); return _genLitIndex; };
private:

  void attach(SaturationAlgorithm* salg);

  struct Entry {
    Index* index;
    int refCnt;
  };
  SaturationAlgorithm* _alg;
  DHMap<IndexType,Entry> _store;

  LiteralIndexingStructure* _genLitIndex;

  Index* create(IndexType t);
};

};// namespace Indexing

#endif // INDEXING_INDEXMANAGER_HPP_
