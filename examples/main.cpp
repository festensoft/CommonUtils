// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

/**
 * @file main.cpp
 * @brief Example application exercising CommonUtils end to end.
 *
 * Run with no arguments for a safe tour of every module. Pass --crash to
 * additionally see StackTrace's real crash-handling path fire (this
 * deliberately terminates the process at the end).
 */

#include "Demos.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
   // The StackTrace demo's crash path writes directly to stderr,
   // unbuffered. Auto-flush std::cout after every write so its output
   // stays in true chronological order relative to that (and to the
   // background threads several demos spin up) instead of appearing
   // wherever its internal buffer happens to flush.
   std::cout << std::unitbuf;

   bool triggerCrash = false;
   for (int i = 1; i < argc; ++i)
   {
      if (std::string(argv[i]) == "--crash")
      {
         triggerCrash = true;
      }
   }

   std::cout << "CommonUtils example application\n"
             << "================================\n\n";

   RunLoggerDemo();
   RunCircularBufferDemo();
   RunTimerDemo();
   RunSnoozableTimerDemo();
   RunDataHandlerDemo();
   RunStackTraceDemo(triggerCrash); // last: may terminate the process

   std::cout << "All demos completed.\n";
   return 0;
}
