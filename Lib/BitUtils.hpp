
/*
 * File BitUtils.hpp.
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
 * @file BitUtils.hpp
 * Defines class BitUtils.
 */


#ifndef __BitUtils__
#define __BitUtils__

#include <string.h>

#include "Lib/Portability.hpp"

namespace Lib {

class BitUtils
{
public:
  /**
   * Return true iff @b sz bytes starting at @b ptr1 are equal to those
   * starting at @b ptr2
   */
  static bool memEqual(const void* ptr1, const void* ptr2, size_t sz)
  {
    return !memcmp(ptr1,ptr2,sz);
  }

  /**
   * Set @b bytes of bytes starting at @ ptr to zero
   */
  static void zeroMemory(void* ptr, size_t bytes)
  {
    size_t* sptr=static_cast<size_t*>(ptr);
    while(bytes>=sizeof(sptr)) {
      *(sptr++)=0;
      bytes-=sizeof(sptr);
    }
    char* cptr=reinterpret_cast<char*>(sptr);
    while(bytes) {
      *(cptr++)=0;
      bytes--;
    }
  }

  /**
   * Return the value of @b index -th bit, starting at the least significant
   * bit of the byte at @b ptr
   */
  inline
  static bool getBitValue(void* ptr, size_t index)
  {
    unsigned char* cptr=static_cast<unsigned char*>(ptr)+index/8;
    return ((*cptr)>>(index&7))&1;
  }

  /**
   * Set the @b index -th bit, starting at the least significant bit of the
   * byte at @b ptr, to @b value
   */
  inline
  static void setBitValue(void* ptr, size_t index, bool value)
  {
    unsigned char* cptr=static_cast<unsigned char*>(ptr)+index/8;
    if(value) {
      *cptr|=1<<(index&7);
    } else {
      *cptr&=~(1<<(index&7));
    }
  }

  /**
   * Return true iff the enabled bits of @b subset are subset of those
   * enabled in @b set
   */
  template<typename T>
  inline static bool isSubset(T set, T subset)
  {
    return (set&subset)==subset;
  }

  /**
   * Reverse the bit order in @b v
   */
  static unsigned reverseBits(unsigned v) __attribute__((const))
  {
    ASS_STATIC(sizeof(unsigned)==4);

    // swap odd and even bits
    v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
    // swap consecutive pairs
    v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
    // swap nibbles ...
    v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
    // swap bytes
    v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
    // swap 2-byte long pairs
    v = ( v >> 16             ) | ( v               << 16);
    return v;
  }

  /**
   * Return the integer part of the base two logarithm of @b v
   *
   * The returned value is actually index of the most significant
   * non-zero bit.
   */
  static unsigned log2(unsigned v) __attribute__((const))
  {
    const unsigned int b[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
    const unsigned int S[] = {1, 2, 4, 8, 16};
    int i;

    /*register*/ // MS: register keyword is deprecated
    unsigned int r = 0; // result of log2(v) will go here
    for (i = 4; i >= 0; i--) // unroll for speed...
    {
      if (v & b[i])
      {
        v >>= S[i];
        r |= S[i];
      }
    }
    return r;
  }

};

};// namespace Lib

#endif // LIB_BITUTILS_HPP_
