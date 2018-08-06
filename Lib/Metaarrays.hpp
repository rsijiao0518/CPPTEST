
/*
 * File Metaarrays.hpp.
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
 * @file Metaarrays.hpp
 * Defines class Metaarrays.
 */


#ifndef __Metaarrays__
#define __Metaarrays__

#include "Forwards.hpp"

#include "VirtualIterator.hpp"

namespace Lib {

/**
 * Provides constant-time copyable wrapper for an array.
 *
 * Useful eg. when DArray should take part in a std::pair
 * structure, that will be passed as parameter to some template
 * method, that requires an array object, not just pointer to it.
 */
template<class Arr>
class ReferencedArrayWrapper
{
public:
  DECL_ELEMENT_TYPE( ELEMENT_TYPE(Arr) );
  explicit ReferencedArrayWrapper(Arr& a) : _a(a) {}
  ///Return @b index-th element of the wrapped array
  template<typename Idx>
  OWN_ELEMENT_TYPE operator[] (Idx index) { return _a[index]; }
  ///Return size of the wrapped array
  size_t size() {return _a.size(); }
private:
  Arr& _a;
};

/**
 * Method, that wraps an array object into constant-time
 * copyable wrapper.
 * @see ReferencedArrayWrapper
 */
template<class Arr>
ReferencedArrayWrapper<Arr> wrapReferencedArray(Arr& a)
{
  return ReferencedArrayWrapper<Arr>(a);
}


template<class Arr, typename Functor>
class MappingArray
{
public:
  DECL_ELEMENT_TYPE(RETURN_TYPE(Functor));
  explicit MappingArray(Arr arr, Functor func)
  : _arr(arr), _func(func) {}

  template<typename Idx>
  RETURN_TYPE(Functor) operator[] (Idx index) { return _func(_arr[index]); }
  size_t size() {return _arr.size(); }
private:
  Arr _arr;
  Functor _func;
};

template<class Arr, typename Functor>
MappingArray<Arr,Functor> getMappingArray(Arr arr, Functor func)
{
  return MappingArray<Arr,Functor>(arr, func);
}


};// namespace Lib

#endif // LIB_METAARRAYS_HPP_
