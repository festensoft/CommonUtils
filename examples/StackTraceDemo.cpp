// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "Demos.h"

#include "StackTrace.h"

#include <iostream>
#include <stdexcept>

void RunStackTraceDemo(bool triggerRealCrash)
{
   std::cout << "=== StackTrace demo ===\n";

   // Non-crash path: capture a formatted stack trace from a caught
   // exception, safe to call from ordinary code.
   try
   {
      throw std::runtime_error("example exception");
   }
   catch (const std::exception&)
   {
      std::cout << "captureStackTrace() from a caught exception:\n"
                << CommonUtils::StackTrace::CaptureStackTrace() << '\n';
   }

   if (!triggerRealCrash)
   {
      std::cout << "(skipping the real crash demo; pass --crash to trigger one)\n\n";
      return;
   }

   // Crash-handling path: installs the platform crash handler (POSIX
   // signals, or on Windows a vectored exception handler + SIGABRT) and a
   // post-crash hook, then deliberately dereferences a null pointer. This
   // terminates the process by design -- that's the whole point of
   // installSignalHandlers().
   CommonUtils::StackTrace::SetPostCrashHook([](int code)
   {
      std::cerr << "  post-crash hook ran, code=" << code << '\n';
   });
   CommonUtils::StackTrace::InstallSignalHandlers();

   std::cout << "Triggering a real crash (null pointer dereference)...\n";
   // Deliberate -- this line only exists to demonstrate the crash handler.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
   volatile int* p = nullptr;
   *p = 1;
#pragma GCC diagnostic pop

   std::cout << "unreachable\n";
}
