#include "ScheduleExecutor.hpp"

#include "Lib/Array.hpp"
#include "Lib/Environment.hpp"
#include "Lib/List.hpp"
#include "Lib/PriorityQueue.hpp"
#include "Lib/System.hpp"
#include "Lib/Sys/Multiprocessing.hpp"
#include "Lib/Timer.hpp"
#include "Shell/Options.hpp"

using namespace CASC;
using namespace Lib;
using namespace Lib::Sys;

#define DECI(milli) (milli/100)

ScheduleExecutor::ScheduleExecutor(ProcessPriorityPolicy *policy, SliceExecutor *executor)
  : _policy(policy), _executor(executor)
{
  CALL("ScheduleExecutor::ScheduleExecutor");
  _numWorkers = getNumWorkers();
}

class Item
{
public:
  Item() : _started(true), _process(-1), _code("") {}
  Item(vstring code)
    : _started(false), _process(-1), _code(code) {}
  Item(pid_t process)
    : _started(true), _process(process), _code("") {}

  bool started() const {return _started;}
  vstring code() const
  {
    ASS(!started());
    return _code;
  }
  pid_t process() const
  {
    ASS(started());
    return _process;
  }

private:
  bool _started;
  pid_t _process;
  vstring _code;
};

bool ScheduleExecutor::run(const Schedule &schedule, int terminationTime)
{
  CALL("ScheduleExecutor::run");

  PriorityQueue<Item> queue;
  Schedule::BottomFirstIterator it(schedule);

  // insert all strategies into the queue
  while(it.hasNext())
  {
    vstring code = it.next();
    float priority = _policy->staticPriority(code);
    queue.insert(priority, code);
  }

  typedef List<pid_t> Pool;
  Pool *pool = Pool::empty();

  bool success = false;
  while(Timer::syncClock(), DECI(env.timer->elapsedMilliseconds()) < terminationTime)
  {
    unsigned poolSize = pool ? Pool::length(pool) : 0;

    // running under capacity, wake up more tasks
    while(poolSize < _numWorkers && !queue.isEmpty())
    {
      Item item = queue.pop();
      pid_t process;
      if(!item.started())
      {
        process = spawn(item.code(), terminationTime);
      }
      else
      {
        process = item.process();
        Multiprocessing::instance()->kill(process, SIGCONT);
      }
      Pool::push(process, pool);
      poolSize++;
    }

    bool stopped, exited;
    int code;
    // sleep until process changes state
    pid_t process = Multiprocessing::instance()
      ->poll_children(stopped, exited, code);

    // child died, remove it from the pool and check if succeeded
    if(exited)
    {
      pool = Pool::remove(process, pool);
      if(!code)
      {
        success = true;
        goto exit;
      }
    }
    // child stopped, re-insert it in the queue
    else if(stopped)
    {
      pool = Pool::remove(process, pool);
      float priority = _policy->dynamicPriority(process);
      queue.insert(priority, Item(process));
    }

    // pool empty and queue exhausted - we failed
    if(!pool && queue.isEmpty())
    {
      goto exit;
    }
  }

exit:
  // kill all running processes first
  Pool::DestructiveIterator killIt(pool);
  while(killIt.hasNext())
  {
    pid_t process = killIt.next();
    Multiprocessing::instance()->killNoCheck(process, SIGKILL);
  }
  return success;
}

unsigned ScheduleExecutor::getNumWorkers()
{
  CALL("ScheduleExecutor::getNumWorkers");

  unsigned cores = System::getNumberOfCores();
  cores = cores < 1 ? 1 : cores;
  unsigned workers = min(cores, env.options->multicore());
  if(!workers)
  {
    workers = cores >= 8 ? cores - 2 : cores;
  }
  return workers;
}

pid_t ScheduleExecutor::spawn(vstring code, int terminationTime)
{
  CALL("ScheduleExecutor::spawn");

  pid_t pid = Multiprocessing::instance()->fork();
  ASS_NEQ(pid, -1);

  // parent
  if(pid)
  {
    return pid;
  }
  // child
  else
  {
    _executor->runSlice(code, terminationTime);
    ASSERTION_VIOLATION; // should not return
  }
}
