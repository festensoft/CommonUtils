// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_STACKTRACE_H_
#define COMMONUTILS_STACKTRACE_H_

/**
 * @file StackTrace.h
 * @brief Cross-platform crash handler that prints a demangled stack trace.
 *
 * Installs crash trapping for the platform's equivalent of SIGSEGV, SIGABRT,
 * and (on POSIX) SIGBUS, using a three-phase approach:
 *
 *  **Phase 1 (as safe as practically possible):** Writes a stack trace
 *  banner and raw frame info directly to stderr using only minimal,
 *  allocation-free calls. This is designed to succeed even if the heap is
 *  corrupted.
 *
 *  **Phase 2 (best-effort):** Invokes the user-supplied PostCrashHook, e.g.
 *  to log the crash via spdlog, dump a trace ring-buffer, and flush log
 *  files. This is NOT guaranteed-safe and may hang if the crash occurred
 *  inside malloc or the logging library itself — but Phase 1 output is
 *  already on stderr. A watchdog ensures the process terminates even if
 *  Phase 2 hangs.
 *
 *  **Phase 3:** Restores default crash disposition and lets the OS
 *  terminate the process (POSIX: re-raise the signal; Windows: continue
 *  exception search / TerminateProcess).
 *
 * Platform notes:
 *  - SIGBUS is POSIX-only; on Windows, hardware faults are reported via a
 *    vectored exception handler instead, covering a different (documented)
 *    set of exception codes (e.g. EXCEPTION_ACCESS_VIOLATION).
 *  - The PostCrashHook's `int` argument is a POSIX signal number on POSIX
 *    builds, but a raw Win32 exception code (or SIGABRT) on Windows builds.
 */

#include <csignal>
#include <functional>
#include <string>
#include <string_view>

#ifdef _WIN32
struct _EXCEPTION_POINTERS; // forward decl, avoids pulling in <windows.h> here
#endif

namespace CommonUtils
{

/**
 * @class StackTrace
 * @brief Installs crash handlers that dump a stack trace on crash.
 *
 * Typical usage — call once early in main(), after logger init:
 * @code
 * CommonUtils::Logging::LoggerConfig config;
 * // ... add sinks ...
 * config.enableTraceLogger = true; // keep a ring buffer of recent log messages
 * CommonUtils::Logging::InitFromConfig(config);
 *
 * CommonUtils::StackTrace::SetPostCrashHook([](int sig)
 * {
 *    GPCRIT("Caught signal {} — see stderr for stack trace", sig);
 *    CommonUtils::Logging::DumpTraceBacktrace(); // recent GPTRACE history
 *    CommonUtils::Logging::Flush();
 * });
 *
 * CommonUtils::StackTrace::InstallSignalHandlers();
 * @endcode
 */
class StackTrace
{
public:
   /** @brief Callback type invoked after the safe Phase 1 output. */
   using PostCrashHook = std::function<void(int /*signal*/)>;

   /**
    * @brief Install crash handlers.
    *
    * On POSIX, registers a signal handler for SIGSEGV, SIGABRT, and SIGBUS.
    * On Windows, registers a vectored exception handler for hardware
    * faults (access violation, stack overflow, etc.) plus a CRT signal
    * handler for SIGABRT. Safe to call more than once — subsequent calls
    * are no-ops.
    */
   static void InstallSignalHandlers();

   /**
    * @brief Register a best-effort callback for Phase 2.
    *
    * The hook is called from inside the crash handler after the
    * best-effort stack trace has been written to stderr. It is wrapped
    * in a try/catch and guarded by a watchdog (alarm() on POSIX, a
    * timed worker thread on Windows), so a hang or throw will not
    * prevent process termination.
    *
    * @param hook Callable taking the signal number (POSIX) or Win32
    *             exception code / SIGABRT (Windows).
    */
   static void SetPostCrashHook(PostCrashHook hook);

   /**
    * @brief Obtain a formatted stack trace string (non-signal-safe).
    *
    * Useful for logging from normal (non-signal) contexts, e.g. on
    * caught exceptions.  NOT safe to call from a signal handler.
    *
    * @param skipFrames Number of innermost frames to skip (default 1
    *                   to omit CaptureStackTrace itself).
    * @return Multi-line string with one frame per line.
    */
   [[nodiscard]] static std::string CaptureStackTrace(int skipFrames = 1);

private:
   /**
    * @brief Write a C string to stderr, avoiding buffered/allocating I/O.
    * @param msg Null-terminated string to write.
    */
   static void WriteStderr(const char* msg);

#ifdef _WIN32
   /** @brief CRT signal handler, used on Windows only for SIGABRT. */
   static void signalHandler(int signal);

   /** @brief Vectored exception handler for hardware faults (Windows only). */
   static long __stdcall vectoredExceptionHandler(_EXCEPTION_POINTERS* exceptionInfo);
#else
   /** @brief The signal handler installed for crash signals. */
   static void SignalHandler(int signal);
#endif

   /** @brief Maximum number of stack frames to capture. */
   static constexpr int MAX_FRAMES = 128;

   /** @brief Seconds before the watchdog kills the process if Phase 2 hangs. */
   static constexpr unsigned int PHASE2_TIMEOUT_SEC = 3;

   /** @brief Guards against multiple installations. */
   static bool s_installed; // NOLINT(readability-identifier-naming)

   /** @brief Optional user-supplied hook for Phase 2 (logger flush, etc.). */
   static PostCrashHook s_postCrashHook; // NOLINT(readability-identifier-naming)
};

} // namespace CommonUtils

#endif // STACKTRACE_H_
