/**
 * @file Semaphore.hpp
 * Defines class Semaphore.
 */

#ifndef __Semaphore__
#define __Semaphore__

#include <sys/types.h>

#include "Forwards.hpp"

#include "Lib/Portability.hpp"

namespace Lib {
namespace Sys {

class Semaphore
{
public:
  Semaphore() : semid(-1), semCnt(0) {};
  explicit Semaphore(int num);
  ~Semaphore();

  Semaphore(const Semaphore& s);
  const Semaphore& operator=(const Semaphore& s);

  bool hasSemaphore() { return semid!=-1; }

  void inc(int num);
  void incp(int num);
  void dec(int num);
  int get(int num);
  void set(int num, int val);

  bool isLastInstance();

private:

  void doInc(int num);
  void doIncPersistent(int num);
  void doDec(int num);
  void doSet(int num, int val);
  int doGet(int num);

  void registerInstance(bool addToInstanceList=true);
  void deregisterInstance();

  void acquireInstance();
  void releaseInstance();

  int semid;
  /** Number of semaphores */
  int semCnt;

  static void releaseAllSemaphores();
  static void postForkInChild();
  static void ensureEventHandlersInstalled();

  typedef List<Semaphore*> SemaphoreList;

  static SemaphoreList* s_instances;

  union semun {
      int              val;    /* Value for SETVAL */
      struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
      unsigned int  *array;  /* Array for GETALL, SETALL */
      struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                  (Linux-specific) */
  };
};

}// namespace Sys
}// namespace Lib

#endif // LIB_SYS_SEMAPHORE_HPP_
