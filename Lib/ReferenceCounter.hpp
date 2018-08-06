
/*
 * File ReferenceCounter.hpp.
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
 * @file ReferenceCounter.hpp
 * Defines class ReferenceCounter.
 */


#ifndef __ReferenceCounter__
#define __ReferenceCounter__

#include "Allocator.hpp"
#include "Debug/Assertion.hpp"

namespace Lib {

class ReferenceCounter
{
public:
  ReferenceCounter()
  {
    _counter=ALLOC_KNOWN(sizeof(Counter), "ReferenceCounter::Counter");
    *_counter=1;
  }
  ReferenceCounter(const ReferenceCounter& rc)
  {
    _counter=rc._counter;
    inc();
  }
  ~ReferenceCounter()
  {
    dec();
  }
  ReferenceCounter& operator=(const ReferenceCounter& rc)
  {
    dec();
    _counter=rc._counter;
    inc();
    return *this;
  }
  bool isLast()
  {
    return !*_counter;
  }
private:
  inline void inc()
  {
    (*_counter)++;
    ASS_NEQ(*_counter,0);
  }
  inline void dec()
  {
    ASS_NEQ(*_counter,0);
    (*_counter)--;
    if(!*_counter) {
      DEALLOC_KNOWN(_counter,sizeof(Counter),"ReferenceCounter::Counter");
    }
  }

  typedef size_t Counter;
  Counter* _counter;
};

};// namespace Lib

#endif // LIB_REFERENCECOUNTER_HPP_
