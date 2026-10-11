// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "Demos.h"
#include "Logger.h"
#include "LoggerMacros.h"
#include "StackTrace.h"

#include <stdexcept>

void RunStackTraceDemo(bool triggerRealCrash)
{
   GPINFO("=== StackTrace demo ===");

   // Non-crash path: capture a formatted stack trace from a caught
   // exception, safe to call from ordinary code.
   try
   {
      throw std::runtime_error("example exception");
   }
   catch (const std::exception&)
   {
      GPINFO("captureStackTrace() from a caught exception:{} ",
             CommonUtils::StackTrace::CaptureStackTrace());
   }

   if (!triggerRealCrash)
   {
      GPINFO("(skipping the real crash demo; pass --crash to trigger one)");
      return;
   }

   // Crash-handling path: installs the platform crash handler (POSIX
   // signals, or on Windows a vectored exception handler + SIGABRT) and a
   // post-crash hook, then deliberately dereferences a null pointer. This
   // terminates the process by design -- that's the whole point of
   // installSignalHandlers().
   CommonUtils::StackTrace::SetPostCrashHook([](int code) {
      CommonUtils::Logging::DumpTraceBacktrace();
      GPERROR("  post-crash hook ran, code= {} ", code);
   });
   CommonUtils::StackTrace::InstallSignalHandlers();

   GPINFO("Triggering a real crash (null pointer dereference)...");
   // Deliberate -- this line only exists to demonstrate the crash handler.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
   volatile int* p = nullptr;
   *p = 1;
#pragma GCC diagnostic pop

   GPCRIT("unreachable");
}
