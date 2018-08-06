
/*
 * File Exception.hpp.
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
 * Class Exception.hpp. Defines Vampire exceptions.
 *
 * @since 03/12/2003, Manchester
 */

#ifndef __Exception__
#define __Exception__

#include <iostream>

#include "LastCopyWatcher.hpp"
#include "VString.hpp"

namespace Lib {

using namespace std;

/**
 * Base for every class that is to be thrown
 */
class ThrowableBase
{
};

/**
 * Abstract class Exception. It is the base class for
 * several concrete classes.
 */
class Exception : public ThrowableBase
{
public:
  /** Create an exception with a given error message */
  explicit Exception (const char* msg)
    : _message(msg)
  { s_exceptionCounter++; }
  Exception (const char* msg, int line);
  explicit Exception (const vstring msg)
    : _message(msg)
  { s_exceptionCounter++; }
  virtual void cry (ostream&);
  virtual ~Exception()
  {
    if(_lcw.isLast()) {
      s_exceptionCounter--;
      ASS_GE(s_exceptionCounter,0);
    }
  }

  static bool isThrown() { return s_exceptionCounter!=0; }
  static bool isThrownDuringExceptionHandling() { return s_exceptionCounter>1; }
  vstring msg() { return _message; }
protected:
  /** Default constructor, required for some subclasses, made protected
   * so that it cannot be called directly */
  Exception () { s_exceptionCounter++; }
  /** The error message */
  vstring _message;

  LastCopyWatcher _lcw;

  /** Number of currently existing Exception objects
   * (not counting copies of the same object) */
  static int s_exceptionCounter;

}; // Exception


/**
 * Class UserErrorException. A UserErrorException is thrown
 * when a user error occurred, for example, a file name is
 * specified incorrectly; an invalid option to Vampire
 * was given, or there is a syntax error in the input file.
 */
class UserErrorException
  : public Exception
{
 public:
  explicit UserErrorException (const char* msg)
    : Exception(msg)
  {}
  explicit UserErrorException (const vstring msg)
    : Exception(msg)
  {}
  void cry (ostream&);
}; // UserErrorException

/**
 * Class ValueNotFoundException. A ValueNotFoundException is
 * thrown when a value is not found in some collection.
 * Currently it is being thrown by the NameArray objects.
 */
class ValueNotFoundException
  : public Exception
{
 public:
   ValueNotFoundException ()
    : Exception("")
  {}
}; // UserErrorException

/**
 * Class MemoryLimitExceededException.
 */
class MemoryLimitExceededException
: public Exception
{
public:
//  MemoryLimitExceededException ()
//  : Exception("The memory limit exceeded")
//  {}
  explicit MemoryLimitExceededException (bool badAlloc=false)
  : Exception(badAlloc?"bad_alloc received":"The memory limit exceeded")
  {}
}; // MemoryLimitExceededException

/**
 * Class TimeLimitExceededException.
 */
class TimeLimitExceededException
: public Exception
{
public:
  TimeLimitExceededException ()
  : Exception("The time limit exceeded")
  {}
}; // TimeLimitExceededException

/**
 * Class ActivationLimitExceededException.
 */
class ActivationLimitExceededException
: public Exception
{
public:
  ActivationLimitExceededException ()
  : Exception("The activation limit exceeded")
  {}
}; // ActivationLimitExceededException


/**
 * Class InvalidOperationException.
 */
class InvalidOperationException
  : public Exception
{
 public:
   explicit InvalidOperationException (const char* msg)
    : Exception(msg)
  {}
   explicit InvalidOperationException (const vstring msg)
    : Exception(msg)
  {}
  void cry (ostream&);
}; // InvalidOperationException

/**
 * Class SystemFailException.
 */
class SystemFailException
  : public Exception
{
public:
  SystemFailException (const vstring msg, int err);
  void cry (ostream&);

  int err;
}; // InvalidOperationException

/**
 * Class NotImplementedException.
 */
class NotImplementedException
  : public Exception
{
 public:
   NotImplementedException (const char* file,int line)
    : Exception(""), file(file), line(line)
  {}
   void cry (ostream&);
 private:
   const char* file;
   int line;
}; // InvalidOperationException


}// namespace Lib

#define VAMPIRE_EXCEPTION \
  throw Lib::Exception(__FILE__,__LINE__)
#define USER_ERROR(msg) \
  throw Lib::UserErrorException(msg)
#define INVALID_OPERATION(msg) \
  throw Lib::InvalidOperationException(msg)
#define SYSTEM_FAIL(msg,err) \
  throw Lib::SystemFailException(msg,err)
#define NOT_IMPLEMENTED \
  throw Lib::NotImplementedException(__FILE__, __LINE__)

#endif // LIB_EXCEPTION_HPP_



