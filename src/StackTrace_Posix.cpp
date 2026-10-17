// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "StackTrace.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unistd.h>

// POSIX backtrace support — available on Linux (glibc) and macOS.
#include <execinfo.h>

// GCC / Clang ABI demangling.
#include <cxxabi.h>

namespace CommonUtils
{

bool StackTrace::s_installed = false;
StackTrace::PostCrashHook StackTrace::s_postCrashHook = nullptr;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void StackTrace::InstallSignalHandlers()
{
   if (s_installed)
   {
      return;
   }
   s_installed = true;

   struct sigaction sa{};
   sa.sa_handler = &StackTrace::SignalHandler;
   sigemptyset(&sa.sa_mask);
   // SA_RESETHAND: restore default disposition after first delivery so the
   //               re-raise in the handler produces the normal behaviour
   //               (core dump / termination).
   // NOLINTNEXTLINE(hicpp-signed-bitwise) — SA_RESETHAND is defined by POSIX
   sa.sa_flags = SA_RESETHAND;

   sigaction(SIGSEGV, &sa, nullptr);
   sigaction(SIGABRT, &sa, nullptr);
   sigaction(SIGBUS, &sa, nullptr);
}

void StackTrace::SetPostCrashHook(PostCrashHook hook) { s_postCrashHook = std::move(hook); }

std::string StackTrace::CaptureStackTrace(int skipFrames)
{
   std::array<void*, MAX_FRAMES> buffer{};
   const int numFrames = backtrace(buffer.data(), MAX_FRAMES);

   // backtrace_symbols allocates with malloc; unique_ptr would be fine but
   // we need to free with free(), so manage manually.
   char** symbols = backtrace_symbols(buffer.data(), numFrames);
   if (symbols == nullptr)
   {
      return "(unable to capture stack trace)\n";
   }

   std::ostringstream oss;
   for (int i = skipFrames; i < numFrames; ++i)
   {
      // Attempt to demangle the symbol.  The raw string from
      // backtrace_symbols looks like:
      //   Linux:  ./binary(+0x1234) [0x...]   or
      //           ./binary(_ZN3Foo3barEv+0x42) [0x...]
      //   macOS:  0  binary  0x...  _ZN3Foo3barEv + 42
      //
      // We extract the mangled name and demangle it.
      const std::string frame(symbols[i]);
      std::string demangled;

      // --- Linux format: look for '(' ... '+' or ')' ---
      auto parenOpen = frame.find('(');
      auto parenPlus = frame.find('+', parenOpen != std::string::npos ? parenOpen : 0);
      auto parenClose = frame.find(')', parenOpen != std::string::npos ? parenOpen : 0);

      if (parenOpen != std::string::npos && parenClose != std::string::npos &&
          parenOpen < parenClose)
      {
         auto end =
             (parenPlus != std::string::npos && parenPlus < parenClose) ? parenPlus : parenClose;
         const std::string mangled = frame.substr(parenOpen + 1, end - parenOpen - 1);
         if (!mangled.empty())
         {
            int status = -1;
            char* realName = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
            if (status == 0 && realName != nullptr)
            {
               demangled = realName;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
            free(realName);
         }
      }

      oss << "  " << (i - skipFrames) << "# ";
      if (!demangled.empty())
      {
         oss << demangled;
         // Append offset if present.
         if (parenPlus != std::string::npos && parenClose != std::string::npos &&
             parenPlus < parenClose)
         {
            oss << frame.substr(parenPlus, parenClose - parenPlus);
         }
      }
      else
      {
         oss << frame;
      }
      oss << '\n';
   }

   // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
   free(static_cast<void*>(symbols));
   return oss.str();
}

// ---------------------------------------------------------------------------
// Signal handler  (async-signal-safe as much as practical)
// ---------------------------------------------------------------------------

void StackTrace::SignalHandler(int signal)
{
   // Only async-signal-safe calls should be used here.
   // backtrace / backtrace_symbols_fd are widely safe in practice on
   // glibc and macOS but are not guaranteed by POSIX.

   WriteStderr("\n========================================\n");
   WriteStderr("  Caught signal: ");

   // Convert signal number to string without printf (async-signal-safe).
   // Signal numbers are small positive ints, so a tiny buffer suffices.
   std::array<char, 16> numBuf{};
   size_t idx = 0;
   int sig = signal;
   if (sig == 0)
   {
      numBuf[idx++] = '0';
   }
   else
   {
      // Build digits in reverse.
      std::array<char, 16> tmp{};
      std::array<char, 16>::size_type tmpIdx = 0;
      while (sig > 0 && tmpIdx < tmp.size())
      {
         const int digit = sig % 10;
         tmp[tmpIdx++] = static_cast<char>('0' + digit);
         sig /= 10;
      }
      for (size_t j = tmpIdx; j > 0; --j)
      {
         numBuf[idx++] = tmp[j - 1];
      }
   }
   numBuf[idx] = '\0';
   WriteStderr(numBuf.data());

   // Also print a human-readable name for the most common signals.
   switch (signal)
   {
      case SIGSEGV:
         WriteStderr(" (SIGSEGV - Segmentation fault)");
         break;
      case SIGABRT:
         WriteStderr(" (SIGABRT - Abort)");
         break;
      case SIGBUS:
         WriteStderr(" (SIGBUS  - Bus error)");
         break;
      default:
         break;
   }

   WriteStderr("\n========================================\n");
   WriteStderr("Stack trace:\n");

   // Capture and write frames directly to stderr (async-signal-safe on
   // glibc and macOS).
   std::array<void*, MAX_FRAMES> buffer{};
   const int numFrames = backtrace(buffer.data(), MAX_FRAMES);
   backtrace_symbols_fd(buffer.data(), numFrames, STDERR_FILENO);

   WriteStderr("========================================\n\n");

   // === Phase 2: Best-effort logger flush (NOT async-signal-safe) ==========
   //
   // This usually works, but may deadlock if the crash occurred inside
   // malloc or spdlog.  Phase 1 output is already on stderr, so a hang
   // here only loses the log-file flush.  The alarm() watchdog ensures
   // the process terminates regardless.
   //
   // NOLINTNEXTLINE(cert-sig30-c) — intentional best-effort non-safe calls
   if (s_postCrashHook)
   {
      // Set a watchdog: if Phase 2 hangs, SIGALRM will fire and
      // terminate the process (SA_RESETHAND already restored defaults
      // for the crash signal, and SIGALRM's default is termination).
      alarm(PHASE2_TIMEOUT_SEC);

      try
      {
         s_postCrashHook(signal);
      }
      catch (...)
      {
         WriteStderr("(post-crash hook threw an exception — ignored)\n");
      }

      // Cancel the watchdog if we made it through.
      alarm(0);
   }

   // === Phase 3: Re-raise for core dump ====================================
   // SA_RESETHAND already restored the default disposition.
   const int raiseResult = raise(signal);
   if (raiseResult != 0)
   {
      WriteStderr("(raise failed, forcing _exit)\n");
      _exit(128 + signal);
   }
}

void StackTrace::WriteStderr(const char* msg)
{
   // write() is async-signal-safe per POSIX.
   // We deliberately ignore the return value in a signal handler context.
   // NOLINTNEXTLINE(bugprone-unused-return-value,cert-err33-c)
   [[maybe_unused]] auto returnValue = write(STDERR_FILENO, msg, strlen(msg));
}

} // namespace CommonUtils
