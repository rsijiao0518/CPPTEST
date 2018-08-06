
/*
 * File Ordering.hpp.
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
 * @file Ordering.hpp
 * Defines (abstract) class Ordering for simplification orderings
 *
 * @since 30/04/2008 flight Brussels-Tel Aviv
 */

#ifndef __Ordering__
#define __Ordering__

#include "Forwards.hpp"

#include "Debug/Assertion.hpp"

#include "Lib/Comparison.hpp"
#include "Lib/SmartPtr.hpp"
#include "Lib/DArray.hpp"

#include "Lib/Allocator.hpp"

namespace Kernel {

using namespace Shell;

/**
 * An abstract class for simplification orderings
 * @since 30/04/2008 flight Brussels-Tel Aviv
 */
class Ordering
{
public:
  CLASS_NAME(Ordering);
  USE_ALLOCATOR(Ordering);

  /**
   * Represents the results of ordering comparisons
   *
   * Values of elements must be equal to values of corresponding elements
   * in the @c ArgumentOrderVals enum, so that one can convert between the
   * enums using static_cast.
   */
  enum Result {
    GREATER=1,
    LESS=2,
    GREATER_EQ=3,
    LESS_EQ=4,
    EQUAL=5,
    INCOMPARABLE=6
  };

  Ordering();
  virtual ~Ordering();

  /** Return the result of comparing @b l1 and @b l2 */
  virtual Result compare(Literal* l1,Literal* l2) const = 0;
  /** Return the result of comparing terms (not term lists!)
   * @b t1 and @b t2 */
  virtual Result compare(TermList t1,TermList t2) const = 0;

  static bool isGorGEorE(Result r) { return (r == GREATER || r == GREATER_EQ || r == EQUAL); }

  virtual Comparison compareFunctors(unsigned fun1, unsigned fun2) const = 0;

  void removeNonMaximal(LiteralList*& lits) const;

  static Result fromComparison(Comparison c);

  static Result reverse(Result r)
  {
    CALL("Ordering::reverse");
    
    switch(r) {
    case GREATER:
      return LESS;
    case GREATER_EQ:
      return LESS_EQ;
    case LESS:
      return GREATER;
    case LESS_EQ:
      return GREATER_EQ;
    case EQUAL:
    case INCOMPARABLE:
      return r;
    default:
      ASSERTION_VIOLATION;
    }
  }
  static const char* resultToString(Result r);

  static Ordering* create(Problem& prb, const Options& opt);

  static bool trySetGlobalOrdering(OrderingSP ordering);
  static Ordering* tryGetGlobalOrdering();

  Result getEqualityArgumentOrder(Literal* eq) const;
protected:

  Result compareEqualities(Literal* eq1, Literal* eq2) const;

private:

  enum ArgumentOrderVals {
    /**
     * Values representing order of arguments in equality,
     * to be stores in the term sharing structure.
     *
     * The important thing is that the UNKNOWN value is
     * equal to 0, as this will be the default value inside
     * the term objects
     *
     * Values of elements must be equal to values of corresponding elements
     * in the @c Result enum, so that one can convert between the
     * enums using static_cast.
     */
    AO_UNKNOWN=0,
    AO_GREATER=1,
    AO_LESS=2,
    AO_GREATER_EQ=3,
    AO_LESS_EQ=4,
    AO_EQUAL=5,
    AO_INCOMPARABLE=6
  };


  void createEqualityComparator();
  void destroyEqualityComparator();

  class EqCmp;
  /** Object used to compare equalities */
  EqCmp* _eqCmp;

  /**
   * We store orientation of equalities in this ordering inside
   * the term sharing structure. Setting an ordering to be global
   * does not change the behavior of Vampire, but may lead to
   * better performance, as the equality orientation will be cached
   * inside the sharing structure.
   */
  static OrderingSP s_globalOrdering;
}; // class Ordering

// orderings that rely on symbol precedence
class PrecedenceOrdering
: public Ordering
{
public:
  Result compare(Literal* l1,Literal* l2) const override;
  Comparison compareFunctors(unsigned fun1, unsigned fun2) const override;

protected:
  // l1 and l2 are not equalities and have the same predicate
  virtual Result comparePredicates(Literal* l1,Literal* l2) const = 0;
  
  PrecedenceOrdering(Problem& prb, const Options& opt);

  Result compareFunctionPrecedences(unsigned fun1, unsigned fun2) const;

  int predicatePrecedence(unsigned pred) const;
  int predicateLevel(unsigned pred) const;

  /** number of predicates in the signature at the time the order was created */
  unsigned _predicates;
  /** number of functions in the signature at the time the order was created */
  unsigned _functions;
  /** Array of predicate levels */
  DArray<int> _predicateLevels;
  /** Array of predicate precedences */
  DArray<int> _predicatePrecedences;
  /** Array of function precedences */
  DArray<int> _functionPrecedences;

  bool _reverseLCM;
};

}

#endif
