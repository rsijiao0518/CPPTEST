
/*
 * File System.hpp.
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
 * @file System.hpp
 * Defines the class System that contains wrappers of some system functions
 * and methods that take care of the system stuff and don't fit anywhere
 * else (handling signals etc...)
 */

#ifndef __System__
#define __System__

#include "Forwards.hpp"

#include "Array.hpp"
#include "List.hpp"
#include "Stack.hpp"
#include "Portability.hpp"
#include "VString.hpp"

#define VAMP_RESULT_STATUS_SUCCESS 0
#define VAMP_RESULT_STATUS_UNKNOWN 1
#define VAMP_RESULT_STATUS_OTHER_SIGNAL 2
#define VAMP_RESULT_STATUS_SIGINT 3
#define VAMP_RESULT_STATUS_UNHANDLED_EXCEPTION 4

namespace Lib {

using namespace std;

typedef void (*SignalHandler)(int);

class System {
public:
//  static void gethostname(char* hostname,int maxlength);
  static void setSignalHandlers();
  static vstring extractFileNameFromPath(vstring str);
  static bool extractDirNameFromPath(vstring path, vstring& dir);

  static vstring guessExecutableDirectory();
  static vstring guessExecutableName();

  static void ignoreSIGINT() { s_shouldIgnoreSIGINT=true; }
  static void heedSIGINT() { s_shouldIgnoreSIGINT=false; }
  static bool shouldIgnoreSIGINT() { return s_shouldIgnoreSIGINT; }

  static void ignoreSIGHUP() { s_shouldIgnoreSIGHUP=true; }
  static void heedSIGHUP() { s_shouldIgnoreSIGHUP=false; }
  static bool shouldIgnoreSIGHUP() { return s_shouldIgnoreSIGHUP; }

  static void addInitializationHandler(VoidFunc proc, unsigned priority=0);
  static void onInitialization();

  static void addTerminationHandler(VoidFunc proc, unsigned priority=0);
  static void onTermination();
  static void terminateImmediately(int resultStatus) __attribute__((noreturn));

  static void registerForSIGHUPOnParentDeath();

  static void readCmdArgs(int argc, char* argv[], StringStack& res);

  /**
   * Collect filenames of all the files occurring in the given directory.
   * Recursive traverse subdirs.
   */
  static void readDir(vstring dirName, Stack<vstring>& filenames);

  /**
   * Register the value of the argv[0] argument of the main function, so that
   * it can be later used to determine the executable directory
   */
  static void registerArgv0(const char* argv0) { s_argv0 = argv0; }

  /**
   * Return the size of system physical memory in bytes
   */
  static int int getSystemMemory();

  /**
   * Return number of CPU cores
   */
  static unsigned getNumberOfCores();

  static bool fileExists(vstring fname);

  static pid_t getPID();

  static int executeCommand(vstring command, vstring input, Stack<vstring>& outputLines);

private:

  static ZIArray<List<VoidFunc>*>& initializationHandlersArray();

  /**
   * True if the @b onInitialization() function was already called
   *
   * If this variable is true, the @b addInitializationHandler()
   * function will immediately call the function that is supposed
   * to be an initialization handler, rather than put it into the
   * handler list. This is done because the functions in the handler
   * list were already called at that point.
   */
  static bool s_initialized;

  /**
   * Lists of functions that will be called before Vampire terminates
   *
   * Functions in lists with lower numbers will be called first.
   */
  static ZIArray<List<VoidFunc>*> s_terminationHandlers;

  static bool s_shouldIgnoreSIGINT;
  static bool s_shouldIgnoreSIGHUP;

  static const char* s_argv0;
};

}// namespace Lib

#endif // LIB_SYSTEM_HPP_
