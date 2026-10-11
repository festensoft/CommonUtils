#include "StackTrace.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace CommonUtils
{

bool StackTrace::s_installed = false;
StackTrace::PostCrashHook StackTrace::s_postCrashHook = nullptr;

namespace
{

// Serializes all DbgHelp calls — the API is documented as not thread-safe.
SRWLOCK g_dbgHelpLock = SRWLOCK_INIT;
bool g_symInitialized = false; // guarded by g_dbgHelpLock

// Guards against a second fault while already handling one; Windows has no
// automatic "revert to default disposition" for a VEH the way POSIX's
// SA_RESETHAND gives us, so this is done explicitly.
std::atomic<bool> g_inCrashHandler{false};

HANDLE g_handlerDoneEvent = nullptr;
PVOID g_vehHandle = nullptr;

void ensureSymInitializedLocked()
{
   if (g_symInitialized)
   {
      return;
   }
   SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
   SymInitialize(GetCurrentProcess(), nullptr, TRUE);
   g_symInitialized = true;
}

// Formats one frame as "  N# functionName+0xOFFSET (file:line)", matching
// the POSIX backend's output shape. Falls back to "module+offset" when no
// symbol is available (stripped binaries, system DLLs without a PDB).
void formatFrame(HANDLE process, void* addr, int displayIndex, std::ostringstream& oss)
{
   alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)]{};
   auto* symbol = reinterpret_cast<PSYMBOL_INFO>(symBuf);
   symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
   symbol->MaxNameLen = MAX_SYM_NAME;

   const auto address = reinterpret_cast<DWORD64>(addr);
   DWORD64 displacement = 0;

   oss << "  " << displayIndex << "# ";

   if (SymFromAddr(process, address, &displacement, symbol))
   {
      oss << symbol->Name << "+0x" << std::hex << displacement << std::dec;

      DWORD lineDisplacement = 0;
      IMAGEHLP_LINE64 line{};
      line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
      if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
      {
         oss << " (" << line.FileName << ":" << line.LineNumber << ")";
      }
   }
   else
   {
      HMODULE module = nullptr;
      const bool haveModule = GetModuleHandleExA(
         GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
         reinterpret_cast<LPCSTR>(addr), &module);

      char moduleName[MAX_PATH]{};
      if (haveModule && module != nullptr && GetModuleFileNameA(module, moduleName, MAX_PATH) > 0)
      {
         const char* base = strrchr(moduleName, '\\');
         base = (base != nullptr) ? base + 1 : moduleName;
         const DWORD64 moduleBase = SymGetModuleBase64(process, address);
         oss << "0x" << std::hex << address << std::dec << " (" << base << "+0x" << std::hex
             << (moduleBase != 0 ? address - moduleBase : 0) << std::dec << ")";
      }
      else
      {
         oss << "0x" << std::hex << address << std::dec;
      }
   }

   oss << '\n';
}

// Captures the current stack (skipping skipFrames innermost frames) and
// returns it fully formatted. Used by both the non-crash capture API and
// the crash handlers below. maxFrames is passed in by the caller (a
// StackTrace member function) since MAX_FRAMES is private.
std::string buildBacktraceString(int skipFrames, int maxFrames)
{
   std::vector<void*> buffer(static_cast<size_t>(maxFrames));
   const USHORT numFrames = CaptureStackBackTrace(
      static_cast<DWORD>(skipFrames), static_cast<DWORD>(maxFrames), buffer.data(), nullptr);

   const HANDLE process = GetCurrentProcess();
   std::ostringstream oss;

   AcquireSRWLockExclusive(&g_dbgHelpLock);
   ensureSymInitializedLocked();
   SymRefreshModuleList(process);
   for (USHORT i = 0; i < numFrames; ++i)
   {
      formatFrame(process, buffer[i], static_cast<int>(i), oss);
   }
   ReleaseSRWLockExclusive(&g_dbgHelpLock);

   return oss.str();
}

// Watchdog body: if the crash handler doesn't finish (SetEvent) within
// timeoutMs, force-terminate. This is the Windows analogue of POSIX's
// alarm(3) watchdog — there is no alarm() equivalent, so a dedicated
// thread stands in for it. Started with raw CreateThread (not std::thread)
// to avoid any CRT/iostream static-init or exception-throwing surprises in
// this fragile context. Covers both Phase 1 (symbolication) and Phase 2
// (the user hook) on Windows, a broader guarantee than POSIX's Phase-2-only
// alarm.
DWORD WINAPI watchdogThreadProc(LPVOID param)
{
   const auto timeoutMs = static_cast<DWORD>(reinterpret_cast<uintptr_t>(param));
   if (WaitForSingleObject(g_handlerDoneEvent, timeoutMs) == WAIT_TIMEOUT)
   {
      TerminateProcess(GetCurrentProcess(), 1);
   }
   return 0;
}

// Returns false if a crash was already being handled — the caller should
// terminate immediately rather than re-enter.
bool enterCrashHandler(DWORD watchdogTimeoutMs)
{
   bool expected = false;
   if (!g_inCrashHandler.compare_exchange_strong(expected, true))
   {
      return false;
   }
   if (g_handlerDoneEvent != nullptr)
   {
      ResetEvent(g_handlerDoneEvent);
      CreateThread(nullptr, 0, &watchdogThreadProc,
                   reinterpret_cast<LPVOID>(static_cast<uintptr_t>(watchdogTimeoutMs)), 0, nullptr);
   }
   return true;
}

void leaveCrashHandler()
{
   if (g_handlerDoneEvent != nullptr)
   {
      SetEvent(g_handlerDoneEvent);
   }
}

const char* exceptionCodeName(DWORD code)
{
   switch (code)
   {
   case EXCEPTION_ACCESS_VIOLATION:      return " (EXCEPTION_ACCESS_VIOLATION)";
   case EXCEPTION_STACK_OVERFLOW:        return " (EXCEPTION_STACK_OVERFLOW)";
   case EXCEPTION_ILLEGAL_INSTRUCTION:   return " (EXCEPTION_ILLEGAL_INSTRUCTION)";
   case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return " (EXCEPTION_ARRAY_BOUNDS_EXCEEDED)";
   case EXCEPTION_INT_DIVIDE_BY_ZERO:    return " (EXCEPTION_INT_DIVIDE_BY_ZERO)";
   case EXCEPTION_DATATYPE_MISALIGNMENT: return " (EXCEPTION_DATATYPE_MISALIGNMENT)";
   case EXCEPTION_PRIV_INSTRUCTION:      return " (EXCEPTION_PRIV_INSTRUCTION)";
   case EXCEPTION_IN_PAGE_ERROR:         return " (EXCEPTION_IN_PAGE_ERROR)";
   default:                              return "";
   }
}

// Hardware faults we actually handle. Everything else (notably MSVC/Itanium
// C++ exceptions, which are themselves SEH exceptions with code
// 0xE06D7363, and debugger events) must be passed through untouched, or
// every throw/catch in the process would route through the crash handler.
bool isHandledFaultCode(DWORD code)
{
   switch (code)
   {
   case EXCEPTION_ACCESS_VIOLATION:
   case EXCEPTION_STACK_OVERFLOW:
   case EXCEPTION_ILLEGAL_INSTRUCTION:
   case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
   case EXCEPTION_INT_DIVIDE_BY_ZERO:
   case EXCEPTION_DATATYPE_MISALIGNMENT:
   case EXCEPTION_PRIV_INSTRUCTION:
   case EXCEPTION_IN_PAGE_ERROR:
      return true;
   default:
      return false;
   }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void StackTrace::installSignalHandlers()
{
   if (s_installed)
   {
      return;
   }
   s_installed = true;

   // Best-effort: reserve extra stack headroom so the handler has room to
   // run after a stack-overflow fault.
   ULONG stackGuaranteeBytes = 16 * 1024;
   SetThreadStackGuarantee(&stackGuaranteeBytes);

   // Suppress the blocking WER "program has stopped working" dialog, so an
   // unattended/CI/server process terminates deterministically once the
   // handler returns EXCEPTION_CONTINUE_SEARCH. Deliberate process-wide
   // choice for headless parity with the POSIX behaviour.
   SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

   g_handlerDoneEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

   // CALL_FIRST so this runs before any VEH/__except frames installed by
   // the application or third-party libraries (e.g. Qt), which could
   // otherwise swallow the exception before it reaches us.
   g_vehHandle = AddVectoredExceptionHandler(1, &StackTrace::vectoredExceptionHandler);

   // abort() never raises an SEH exception, so it's invisible to the
   // vectored exception handler above — the CRT signal path is the only
   // way to catch it. Hardware faults never reliably invoke a
   // signal(SIGSEGV, ...) handler on Windows, so the two mechanisms cover
   // disjoint cases and both are needed.
   signal(SIGABRT, &StackTrace::signalHandler);
}

void StackTrace::setPostCrashHook(PostCrashHook hook)
{
   s_postCrashHook = std::move(hook);
}

std::string StackTrace::captureStackTrace(int skipFrames)
{
   return buildBacktraceString(skipFrames, MAX_FRAMES);
}

// ---------------------------------------------------------------------------
// Crash handlers
// ---------------------------------------------------------------------------

void StackTrace::signalHandler(int signal)
{
   if (!enterCrashHandler(PHASE2_TIMEOUT_SEC * 1000))
   {
      TerminateProcess(GetCurrentProcess(), static_cast<UINT>(128 + signal));
      return;
   }

   writeStderr("\n========================================\n");
   writeStderr("  Caught signal: SIGABRT (Abort)\n");
   writeStderr("========================================\n");
   writeStderr("Stack trace:\n");
   writeStderr(buildBacktraceString(0, MAX_FRAMES).c_str());
   writeStderr("========================================\n\n");

   // === Phase 2: best-effort hook, NOT guaranteed-safe on this path ======
   if (s_postCrashHook)
   {
      try
      {
         s_postCrashHook(signal);
      }
      catch (...)
      {
         writeStderr("(post-crash hook threw an exception — ignored)\n");
      }
   }

   leaveCrashHandler();

   // === Phase 3: terminate ================================================
   // There's no raise()-then-default-disposition equivalent for a
   // CRT-emulated signal on Windows; terminate directly (parity with the
   // POSIX backend's _exit(128 + signal) fallback path).
   TerminateProcess(GetCurrentProcess(), static_cast<UINT>(128 + signal));
}

long __stdcall StackTrace::vectoredExceptionHandler(_EXCEPTION_POINTERS* exceptionInfo)
{
   const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
   if (!isHandledFaultCode(code))
   {
      return EXCEPTION_CONTINUE_SEARCH;
   }

   if (!enterCrashHandler(PHASE2_TIMEOUT_SEC * 1000))
   {
      TerminateProcess(GetCurrentProcess(), code);
      return EXCEPTION_CONTINUE_SEARCH; // unreachable
   }

   writeStderr("\n========================================\n");
   writeStderr("  Caught Windows exception code: 0x");
   {
      std::array<char, 16> buf{};
      snprintf(buf.data(), buf.size(), "%08lX", static_cast<unsigned long>(code));
      writeStderr(buf.data());
   }
   writeStderr(exceptionCodeName(code));
   writeStderr("\n========================================\n");

   if (code == EXCEPTION_STACK_OVERFLOW)
   {
      // Minimal path: symbolication needs stack headroom we may not have
      // after a stack overflow.
      writeStderr("Stack trace unavailable (stack overflow)\n");
   }
   else
   {
      writeStderr("Stack trace:\n");
      writeStderr(buildBacktraceString(0, MAX_FRAMES).c_str());
   }
   writeStderr("========================================\n\n");

   // === Phase 2: best-effort hook, NOT guaranteed-safe on this path ======
   if (s_postCrashHook)
   {
      try
      {
         s_postCrashHook(static_cast<int>(code));
      }
      catch (...)
      {
         writeStderr("(post-crash hook threw an exception — ignored)\n");
      }
   }

   leaveCrashHandler();

   // === Phase 3: let default handling terminate the process ==============
   // No raise()-then-default-disposition equivalent exists; continuing the
   // search lets Windows' default unhandled-exception behaviour run, which
   // terminates the process with the exception code as exit status — the
   // correct analogue of POSIX's restore-default-and-reraise.
   return EXCEPTION_CONTINUE_SEARCH;
}

void StackTrace::writeStderr(const char* msg)
{
   const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
   if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
   {
      return;
   }
   DWORD written = 0;
   WriteFile(handle, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
}

} // namespace CommonUtils
