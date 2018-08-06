
/*
 * File Exception.cpp.
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
 * Class Exception.cpp. Implements Vampire exceptions.
 *
 * @since 03/12/2003, Manchester
 */

#include <string.h>

#include "Int.hpp"

#include "Exception.hpp"
#include "VString.hpp"

namespace Lib
{

int Exception::s_exceptionCounter=0;

Exception::Exception (const char* msg, int line)
  : _message((vstring(msg)+": "+Int::toString(line)).c_str())
{ s_exceptionCounter++; }

/**
 * Write a description of the exception to a stream.
 */
void Exception::cry (ostream& str)
{
  str << _message << endl;
} // Exception::cry


/**
 * Write a description of the exception to a stream.
 */
void UserErrorException::cry (ostream& str)
{
  str << "User error: " << _message << endl;
} // UserErrorException::cry

/**
 * Write a description of the exception to a stream.
 */
void InvalidOperationException::cry (ostream& str)
{
  str << "Invalid operation: " << _message << endl;
} // InvalidOperationException::cry


SystemFailException::SystemFailException(const vstring msg, int err)
: Exception(msg+" error "+Int::toString(err)+": "+strerror(err)), err(err)
{
//#if VDEBUG
//  LOGS("system fail exception thrown");
//#endif
}
/**
 * Write a description of the exception to a stream.
 */
void SystemFailException::cry (ostream& str)
{
  str << "System fail: " << _message << endl;
} // SystemFailException::cry


/**
 * Write a description of the exception to a stream.
 */
void NotImplementedException::cry (ostream& str)
{
  str << "Not implemented at " << file << ":" << line << endl;
} // NotImplementedException::cry


}// namespace Lib
