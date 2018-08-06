
/*
 * File SmartPtr.hpp.
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
 * @file SmartPtr.hpp
 * Defines class SmartPtr for smart pointers
 *
 * @since 08/05/2007 Manchester
 */

#ifndef __SmartPtr__
#define __SmartPtr__

#include "Forwards.hpp"

#include "Debug/Assertion.hpp"
#include "Debug/Tracer.hpp"

#include "Lib/Allocator.hpp"

namespace Lib
{

template<typename T>
class SmartPtr {
private:
  struct RefCounter {
    CLASS_NAME(SmartPtr::RefCounter);
    USE_ALLOCATOR(SmartPtr::RefCounter);
  
    inline explicit RefCounter(int v) : _val(v) {}
  
    int _val;
  };

public:
  inline
  SmartPtr() : _obj(0), _refCnt(0) {}
  /**
   * Create a smart pointer containing pointer @b obj. If @b nondisposable
   * is true, the object will not be destroyed even after all references to
   * it are gone.
   */
  inline
  explicit SmartPtr(T* obj, bool nondisposable=false)
  : _obj(obj), _refCnt(nondisposable ? 0 : new RefCounter(1)) {ASS(obj);}
  inline
  SmartPtr(const SmartPtr& ptr) : _obj(ptr._obj), _refCnt(ptr._refCnt)
  {
    if(_obj && _refCnt) {
      (_refCnt->_val)++;
    }
  }
  inline
  ~SmartPtr()
  {
    if(!_obj || !_refCnt) {
      return;
    }
    (_refCnt->_val)--;
    ASS(_refCnt->_val >= 0);
    if(! _refCnt->_val) {
      checked_delete(_obj);
      delete _refCnt;
    }
  }
  SmartPtr& operator=(const SmartPtr& ptr)
  {
    CALL("SmartPtr::operator=");

    T* oldObj=_obj;
    RefCounter* oldCnt=_refCnt;
    _obj=ptr._obj;
    _refCnt=ptr._refCnt;

    if(_obj && _refCnt) {
      (_refCnt->_val)++;
    }

    if(oldObj && oldCnt) {
      (oldCnt->_val)--;
      ASS(oldCnt->_val >= 0);
      if(! oldCnt->_val) {
        checked_delete(oldObj);
        delete oldCnt;
      }
    }
    return *this;
  }

  inline
  operator bool() const { return _obj; }

  inline
  T* operator->() const { return _obj; }
  inline
  T& operator*() const { return *_obj; }

  inline
  T* ptr() const { return _obj; }

  inline
  T& ref() const { return *_obj; }

  inline
  bool isEmpty() const { return !_obj; }

  template<class Target>
  inline
  Target* pcast() const { return static_cast<Target*>(_obj); }
private:
  template<typename U> friend class SmartPtr;

  T* _obj;
  RefCounter* _refCnt;
};

};// namespace Lib

#endif // LIB_SMARTPTR_HPP_
