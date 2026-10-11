#include "Logger.h"

#include "LogUtils.h"
#include "SinkFactory.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace CommonUtils
{
namespace Logging
{
namespace
{

// The hot log path reads raw logger pointers through relaxed-cost atomic
// loads: no lock, no refcount traffic. To keep those pointers valid for the
// whole process, every logger ever installed is also kept in this registry
// and never freed -- reconfiguring is rare, and a logger can still be in use
// on another thread (or from a static destructor) when it is replaced.
// Do not "fix" this by moving to atomic<shared_ptr>: pre-C++20 libstdc++/libc++
// implement it with an internal spinlock table on every read, which is the
// exact cost this design avoids.
//
// The registry itself is heap-allocated and never destroyed, so it outlives
// every other static, and is reachable from a static pointer (so it is not a
// leak as far as analyzers are concerned). A function-local static is also
// the standard fix for init order: log calls can happen from other
// translation units' static initializers.
struct Registry
{
   std::mutex mutex;
   std::vector<std::shared_ptr<spdlog::logger>> loggers;
};

Registry& GetRegistry()
{
   static auto* const INSTANCE = new Registry();
   return *INSTANCE;
}

// Keeps `logger` alive for the life of the process and returns the raw
// pointer to publish.
spdlog::logger* Retain(std::shared_ptr<spdlog::logger> logger)
{
   Registry& registry = GetRegistry();
   spdlog::logger* const raw = logger.get();
   const std::lock_guard<std::mutex> lock(registry.mutex);
   registry.loggers.push_back(std::move(logger));
   return raw;
}

// Pre-init fallback: spdlog's own built-in default logger (colored console)
// rather than a hand-configured one. spdlog constructs it lazily and safely,
// so this is fine to call from a static initializer in any translation unit.
spdlog::logger* InitialLogger()
{
   static spdlog::logger* const INITIAL = Retain(spdlog::default_logger());
   return INITIAL;
}

std::atomic<bool> loggerConfigured{false};
std::atomic<spdlog::logger*> currentLogger{InitialLogger()};
std::atomic<spdlog::logger*> traceLogger{nullptr};

spdlog::logger& Current() { return *currentLogger.load(std::memory_order_acquire); }

} // namespace

bool InitFromConfig(const LoggerConfig& config)
{
   std::string buildError;
   std::shared_ptr<spdlog::logger> general;
   std::shared_ptr<spdlog::logger> trace;
   try
   {
      general = BuildLogger(config, &buildError);
      if (general && config.enableTraceLogger)
      {
         trace = BuildTraceLogger(config, &buildError);
      }
   }
   catch (const std::exception& e)
   {
      buildError += std::string("exception: ") + e.what();
      general.reset();
   }

   if (!general || (config.enableTraceLogger && !trace))
   {
      Current().error("InitFromConfig had no effect: " + buildError);
      return false;
   }

   if (loggerConfigured.load(std::memory_order_acquire))
   {
      Current().warn("InitFromConfig called more than once; previous config replaced.");
   }

   spdlog::logger* const newTrace = trace ? Retain(std::move(trace)) : nullptr;
   traceLogger.store(newTrace, std::memory_order_release);
   currentLogger.store(Retain(std::move(general)), std::memory_order_release);
   loggerConfigured.store(true, std::memory_order_release);
   return true;
}

void Flush()
{
   Current().flush();
   if (spdlog::logger* const trace = traceLogger.load(std::memory_order_acquire))
   {
      trace->flush();
   }
}

void DumpTraceBacktrace()
{
   if (spdlog::logger* const trace = traceLogger.load(std::memory_order_acquire))
   {
      trace->dump_backtrace();
      trace->flush();
   }
}

bool ShouldLog(Level level)
{
   if (level == Level::Trace && traceLogger.load(std::memory_order_acquire) != nullptr)
   {
      return true;
   }
   return Current().should_log(ToSpdlogLevel(level));
}

void LogAt(Level level, const SourceLocation& location, const std::string& message)
{
   const spdlog::source_loc loc(location.file, location.line, location.function);
   if (level == Level::Trace)
   {
      if (spdlog::logger* const trace = traceLogger.load(std::memory_order_acquire))
      {
         trace->log(loc, spdlog::level::trace, message);
         return;
      }
   }
   Current().log(loc, ToSpdlogLevel(level), message);
}

void LogTrace(const std::string& message) { LogAt(Level::Trace, SourceLocation(), message); }

void LogDebug(const std::string& message) { LogAt(Level::Debug, SourceLocation(), message); }

void LogInfo(const std::string& message) { LogAt(Level::Info, SourceLocation(), message); }

void LogWarn(const std::string& message) { LogAt(Level::Warn, SourceLocation(), message); }

void LogError(const std::string& message) { LogAt(Level::Error, SourceLocation(), message); }

void LogCrit(const std::string& message) { LogAt(Level::Critical, SourceLocation(), message); }

bool IsInitialized() { return loggerConfigured.load(std::memory_order_acquire); }

void Shutdown()
{
   traceLogger.store(nullptr, std::memory_order_release);
   currentLogger.store(InitialLogger(), std::memory_order_release);
   loggerConfigured.store(false, std::memory_order_release);
}

} // namespace Logging
} // namespace CommonUtils
