
/*
 * File DArray.hpp.
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
 * @file DArray.hpp
 * Defines a class of self-deallocating arrays. They should be used instead
 * of Array when the size is known in advance
 *
 * @since 30/12/2007 Manchester
 */

#ifndef __DArray__
#define __DArray__

#include <algorithm>

#include "Forwards.hpp"
#include "Debug/Assertion.hpp"

#include "Allocator.hpp"
#include "Comparison.hpp"
#include "Exception.hpp"
#include "Random.hpp"
#include "Reflection.hpp"
#include "VirtualIterator.hpp"

namespace Lib {

/**
 * Class of fixed-size self-deallocating generic arrays.
 * @param C the type of content
 * @since 30/12/2007 Manchester
 */
template<typename C>
class DArray
{
private:
  //private and undefined operator= to avoid an implicitly generated one
  DArray& operator=(const DArray&);
public:
  CLASS_NAME(DArray<C>);
  USE_ALLOCATOR(DArray<C>);

  class Iterator;

  DECL_ELEMENT_TYPE(C);
  DECL_ITERATOR_TYPE(Iterator);
  /**
   * Create an array having the given @b size
   * @since 30/12/2007 Manchester
   */
  inline
  explicit DArray (size_t size=0)
    : _size(size), _capacity(size)
  {
    CALL("DArray::DArray");

    if(size>0) {
      void* mem = ALLOC_KNOWN(sizeof(C)*_capacity,"DArray<>");
      _array = array_new<C>(mem, _capacity);
    } else {
      _array=0;
    }
  }

  DArray(const DArray& o)
    : _size(o.size()), _capacity(o.size())
  {
    CALL("DArray::DArray(const DArray&)");

    if(_size==0) {
      _array=0;
      return;
    }
    void* mem = ALLOC_KNOWN(sizeof(C)*_capacity,"DArray<>");
    _array = static_cast<C*>(mem);
    for(size_t i=0; i<_size; i++) {
      new(&_array[i]) C(o[i]);
    }
  }


  /** Delete array */
  inline ~DArray()
  {
    CALL("DArray::~DArray");

    if(_array) {
      array_delete(_array, _capacity);
      DEALLOC_KNOWN(_array,sizeof(C)*_capacity,"DArray<>");
    }
  }

  /** Return a reference to the n-th element of the array */
  inline C& operator[] (size_t n)
  {
    CALL("DArray::operator[]");

    ASS_L(n,_size);
    return _array[n];
  } // operator[]

  /** Return a reference to the n-th element of the array */
  inline const C& operator[](size_t n) const
  {
    CALL("DArray::operator[] const");

    ASS_L(n,_size);
    return _array[n];
  }
 /**
   * Set array's size to @b s. @b s must be smaller or equal to the
   * current array size.
   */
  inline void shrink(size_t s)
  {
    CALL("DArray::shrink");
    ASS_LE(s,_size);

    _size = s;
  }
  
  inline bool operator==(const DArray& o) const
  {
    CALL("DArray::operator==");
    if(size()!=o.size()) { return false; }
    size_t sz = size();
    for(size_t i=0; i<sz; i++) {
      if(!((*this)[i]==o[i])) { return false; }
    }
    return true;
  }

  inline bool operator!=(const DArray& o) const { return !(*this==o); }

  /** return the standard array represented by this DArray */
  inline C* array() { return _array; }
  inline const C* array() const { return _array; }

  inline C* begin() { return _array; }
  inline C* end() { return _array+_size; }



  /**
   * Set array's size to @b s and that its capacity is at least @b s.
   * Return true iff array was not extended.
   * If the capacity is smaller, the array will be extended.
   *
   * @warning upon extension no copying of elements will be done
   *   so the operation is safe only before processing the array.
   * @since 02/01/2008 Manchester
   */
  inline bool ensure(size_t s)
  {
    CALL("DArray::ensure");

    if (_capacity >= s) {
      _size = s;
      return true;
    }

    size_t newCapacity = max(s, _capacity*2);

    void* mem = ALLOC_KNOWN(sizeof(C)*newCapacity,"DArray<>");
    C* newArray=array_new<C>(mem, newCapacity);

    if(_array) {
      array_delete(_array, _capacity);
      DEALLOC_KNOWN(_array,sizeof(C)*_capacity,"DArray<>");
    }
    _size = s;
    _capacity = newCapacity;
    _array = newArray;
    return false;
  } // ensure

  /**
   * Set array's size to @b s and that its capacity is at least @b s.
   * If the capacity is smaller, the array will expand, and all old
   * elements will be copied to the new array.
   *
   */
  void expand(size_t s)
  {
    CALL("DArray::expand");

    if (_capacity >= s) {
      _size = s;
      return;
    }

    size_t oldCapacity=_capacity;
    size_t newCapacity=max(s, oldCapacity*2);
    void* mem = ALLOC_KNOWN(sizeof(C)*newCapacity,"DArray<>");

    C* oldArr = _array;

    _capacity = newCapacity;
    _array = static_cast<C*>(mem);

    C* optr=oldArr;
    C* nptr=_array;
    C* firstEmpty=_array+_size;
    C* afterLast=_array+_capacity;

    while(nptr!=firstEmpty) {
      Relocator<C>::relocate(optr++, nptr++);
    }
    while(nptr!=afterLast) {
      new(nptr++) C();
    }
    _size = s;

    if(oldArr) {
      DEALLOC_KNOWN(oldArr,sizeof(C)*oldCapacity,"DArray<>");
    }

  } // expand

  /**
   * Set array's size to @b s and that its capacity is at least @b s.
   * If the capacity is smaller, the array will expand, and all old
   * elements will be copied to the new array.
   *
   */
  void expand(size_t s, C defVal)
  {
    CALL("DArray::expand/2");

    size_t oldSize = _size;
    expand(s);

    if(s<=oldSize) {
      return;
    }

    for(size_t i=oldSize; i<s; i++) {
      (*this)[i] = defVal;
    }
  } // expand

  /** Return the ensured size of the array */
  inline size_t size() const { return _size; }

  /** Ensure that the array's size is at least @b count and
   * initialize first @b count elements of the array to @b value. */
  void init(size_t count, const C& value=C()) {
    CALL("DArray::init");

    ensure(count);
    C* ptr=_array+count;
    while(ptr!=_array) {
      *(--ptr)=value;
    }
  }

  /**
   * Ensure that the array's size is at least @b count and
   * initialize first @b count elements with values from @b src.
   *
   * @b src has to support C operator[](size_t).
   */
  template<typename Arr>
  void initFromArray(size_t count, const Arr& src) {
    CALL("DArray::initFromArray");

    ensure(count);
    C* ptr=_array+count;
    while(count) {
      *(--ptr)=src[--count];
    }
  }
  void initFromArray(size_t count, const C* src) {
    CALL("DArrayTKV::initFromArray");

    ensure(count);
    C* ptr=_array+count;
    while(count) {
      *(--ptr)=src[--count];
    }
  }

  /**
   * Initialize array by elements of the iterator. Size of the array
   * will be equal to number of elements of the iterator.
   */
  template<class It>
  void initFromIterator(It it, size_t count=0) {
    CALL("DArray::initFromIterator");

    if(count) {
      ensure(count);
      C* ptr=_array;
      while(it.hasNext()) {
	*(ptr++)=it.next();
      }
    } else {
      ensure(0);
      count=0;
      while(it.hasNext()) {
	expand(++count);
	(*this)[count-1]=it.next();
      }
    }
  }


  /**
   * Sort first @b count items using @b Comparator::compare
   * as comparator.
   */
  template<typename Comparator>
  inline void sort(Comparator comp)
  {
    sortGen<false>(comp);
  }

  template<typename Comparator>
  inline void sortInversed(Comparator comp)
  {
    sortGen<true>(comp);
  }

  template<bool Inversed, typename Comparator>
  void sortGen(Comparator comp)
  {
    CALL("DArray::sortGen");

    if(_size <= 1) {
      return;
    }

    // array behaves as a stack of calls to quicksort
    static DArray<size_t> ft(32);

    size_t from = 0;
    size_t to=size()-1;
    ft.ensure(to);

    size_t p = 0; // pointer to the next element in ft
    for (;;) {
      ASS(from<size() && to<size()); //checking for underflows
      ASS(from<to);
      // invariant: from < to
      size_t m = from + Random::getInteger(to-from+1);
      C mid = (*this)[m];
      size_t l = from;
      size_t r = to;
      while (l < m) {
        switch ((Inversed?-1:1)*comp.compare((*this)[l],mid))
  	{
  	case EQUAL:
  	case LESS:
  	  l++;
  	  break;
  	case GREATER:
  	  if (m == r) {
  	    (*this)[m] = (*this)[l];
  	    (*this)[l] = (*this)[m-1];
  	    (*this)[m-1] = mid;
  	    m--;
  	    r--;
  	  }
  	  else {
  	    ASS(m < r);
  	    C aux = (*this)[l];
  	    (*this)[l] = (*this)[r];
  	    (*this)[r] = aux;
  	    r--;
  	  }
  	  break;
  	}
      }
      // l == m
      // now literals in lits[from ... m-1] are smaller than lits[m]
      // and literals in lits[r+1 ... to] are greater than lits[m]
      while (m < r) {
        switch ((Inversed?-1:1)*comp.compare(mid,(*this)[m+1]))
  	{
  	case LESS:
  	  {
  	    C aux = (*this)[r];
  	    (*this)[r] = (*this)[m+1];
  	    (*this)[m+1] = aux;
  	    r--;
  	  }
  	  break;
  	case EQUAL:
  	case GREATER:
  	  (*this)[m] = (*this)[m+1];
  	  (*this)[m+1] = mid;
  	  m++;
  	}
      }
      // now literals in lits[from ... m-1] are smaller than lits[m]
      // and all literals in lits[m+1 ... to] are greater than lits[m]
      if (m+1 < to) {
        ft[p++] = m+1;
        ft[p++] = to;
      }

      to = m-1;
      if (m!=0 && from < to) {
        continue;
      }
      if (p != 0) {
        p -= 2;
        ASS(p >= 0);
        from = ft[p];
        to = ft[p+1];
        continue;
      }
      return;
    }
  }

protected:
  /** current array's size */
  size_t _size;
  /** capacity of currently allocated piece of memory */
  size_t _capacity;
  /** array's content */
  C* _array;

public:
  class Iterator
  {
  public:
    DECL_ELEMENT_TYPE(C);
    inline Iterator() : _next(0), _afterLast(0) {}
    explicit inline Iterator(DArray& arr) : _next(arr._array),
    _afterLast(arr._array+arr._size) {}
    inline bool hasNext() { return _next!=_afterLast; }
    inline C next() { return *(_next++); }
  private:
   C* _next;
   C* _afterLast;
  };
  class ConstIterator
  {
  public:
    DECL_ELEMENT_TYPE(C);
    inline ConstIterator() : _next(0), _afterLast(0) {}
    explicit inline ConstIterator(const DArray& arr) : _next(arr._array),
    _afterLast(arr._array+arr._size) {}
    inline bool hasNext() { return _next!=_afterLast; }
    inline const C& next() { return *(_next++); }
  private:
   const C* _next;
   const C* _afterLast;
  };
  class ReversedIterator
  {
  public:
    DECL_ELEMENT_TYPE(C);
    explicit inline ReversedIterator(DArray& arr) : _curr(arr._array+arr._size),
    _first(arr._array) {}
    inline bool hasNext() { return _curr!=_first; }
    inline C next() { return *(--_curr); }
  private:
    C* _curr;
    C* _first;
  };
}; // class DArray

template<typename T>
VirtualIterator<T> getContentIterator(DArray<T>& arr)
{
  return pvi( typename DArray<T>::Iterator(arr) );
}

} // namespace Lib

#endif // LIB_DARRAY_HPP_
