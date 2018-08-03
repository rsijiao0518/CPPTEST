
/*
 * File BufferedSolver.hpp.
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
 * @file BufferedSolver.hpp
 * Defines class BufferedSolver.
 *
 * The idea is to buffer clauses that are true, or can be made true, by the ground model
 *
 * @author Giles
 */

#ifndef __BufferedSolver__
#define __BufferedSolver__

#include "Forwards.hpp"

#include "Lib/DArray.hpp"
#include "Lib/DHMap.hpp"
#include "Lib/DHSet.hpp"
#include "Lib/ScopedPtr.hpp"
#include "Lib/Stack.hpp"

#include "SATSolver.hpp"

namespace SAT {

using namespace Lib;

class BufferedSolver : public SATSolver {
public:
  CLASS_NAME(BufferedSolver);
  USE_ALLOCATOR(BufferedSolver);

  BufferedSolver(SATSolver* inner);

  virtual SATClause* getRefutation() override { return _inner->getRefutation(); }
  virtual SATClauseList* getRefutationPremiseList() override {
    return _inner->getRefutationPremiseList();
  }
  virtual void randomizeForNextAssignment(unsigned maxVar) override {
    _inner->randomizeForNextAssignment(maxVar);

    // This is not ideal, but we can't wait till solve, because
    // BufferedSolver would not be forced to consult inner if it already "has an assignment in mind"
    flushUnadded();
    _lastStatus = _inner->solve();
  }

  virtual void addClause(SATClause* cl) override;
  virtual Status solve(unsigned conflictCountLimit) override;
  virtual VarAssignment getAssignment(unsigned var) override;

  virtual bool isZeroImplied(unsigned var) override {
    CALL("BufferedSolver::isZeroImplied");
    ASS_G(var,0); ASS_LE(var,_varCnt);
    // alternatively, we could directly refer to _inner, it must handle variables up to _varCnt as well
    return (var > _varCntInnerOld) ? false : _inner->isZeroImplied(var);
  }
  virtual void collectZeroImplied(SATLiteralStack& acc) override { _inner->collectZeroImplied(acc); }
  virtual SATClause* getZeroImpliedCertificate(unsigned var) override { return _inner->getZeroImpliedCertificate(var); }

  virtual void ensureVarCount(unsigned newVarCnt) override { _inner->ensureVarCount(newVarCnt); _varCnt=max(_varCnt,newVarCnt); }
  virtual unsigned newVar() override { 
    CALL("BufferedSolver::newVar");
    
    ALWAYS(_inner->newVar() == ++_varCnt);
    return _varCnt;
  }
  
  virtual void suggestPolarity(unsigned var,unsigned pol) override { _inner->suggestPolarity(var,pol); }
  virtual void recordSource(unsigned var, Literal* lit) override {
    _inner->recordSource(var,lit);
  }

private:

  // check if @cl is implied by current model, and record it
  // if we choose not to add it at this point
  bool checkAndRecordImplied(SATClause* cl);

  // Add any clauses that have been buffered to _inner and solve.
  void flushUnadded();

  SATSolverSCP _inner;

 /**
  * A buffer for new literals that do not yet appear in the solver
  */
  DHMap<unsigned, bool> _literalBuffer;

  /**
   * Clauses that have not been added to _inner as they are either implied by the assignment of _inner
   * or the variables implicitly set in _literalBuffer
   */
  SATClauseStack _unadded;
  
  /**
   * Index (to _unadded) of the least clause not yet checked wrt the current model.
   */ 
  unsigned _checkedIdx;

  /**
   * Remember the last status returned by solve.
   */
  Status _lastStatus;

  /**
   * Our current varCnt and (of _inner as well).
   */
  unsigned _varCnt;
  
  /**
  * varCnt of _inner at the time of the last flush.
  *
  * Variables larger than this are handled by Buffered
  */
  unsigned _varCntInnerOld;
};

}

#endif // __BufferedSolver__
