// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_LOGGING_LOGGER_H_
#define COMMONUTILS_LOGGING_LOGGER_H_

#include "LoggerConfig.h"

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace CommonUtils
{
namespace Logging
{

enum class Level : std::uint8_t
{
   Trace,
   Debug,
   Info,
   Warn,
   Error,
   Critical
};

// Plain POD, deliberately not spdlog::source_loc -- spdlog stays an
// implementation detail hidden from anything that only includes this header.
struct SourceLocation
{
   SourceLocation() : file(nullptr), line(0), function(nullptr) {}

   SourceLocation(const char* fileIn, int lineIn, const char* functionIn)
       : file(fileIn), line(lineIn), function(functionIn)
   {
   }

   const char* file;
   int line;
   const char* function;
};

namespace Detail
{

// The format string must be a compile-time literal (std::format_string
// validates it against Args... at compile time -- an arg-count or type
// mismatch is a build error, not a runtime surprise). To log arbitrary
// runtime content, pass it as an argument rather than as the format
// string: GPINFO("{}", dynamicString), never GPINFO(dynamicString).
template <typename... Args>
std::string FormatMessage(std::format_string<Args...> fmt, Args&&... args)
{
   return std::format(fmt, std::forward<Args>(args)...);
}

} // namespace Detail

// Until this succeeds, logging goes to a built-in fallback logger (spdlog's
// default colored console logger), so GP* macros work from the first line of
// main(). Returns false, leaving the current logger untouched, if the config
// has no usable sinks, names an unknown level, or a sink can't be created
// (e.g. an unwritable log file); the reason is reported through the current
// logger.
bool InitFromConfig(const LoggerConfig& config);
void Flush();

// Writes out the trace logger's ring buffer (see LoggerConfig::enableTraceLogger)
// and flushes it. Does nothing if the trace logger isn't enabled.
void DumpTraceBacktrace();

// True when the current logger would emit at this level. Perf-critical: a
// single atomic load plus a comparison. The GP* macros gate on this so a
// disabled call's arguments and message formatting are never evaluated.
// Always true while the trace logger is enabled, since its ring buffer
// records every message at every level.
bool ShouldLog(Level level);
void LogAt(Level level, const SourceLocation& location, const std::string& message);

void LogTrace(const std::string& message);
void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);
void LogCrit(const std::string& message);

bool IsInitialized();

// Resets to the pre-init fallback logger. Intended for test teardown so
// module state doesn't leak across tests in the same binary.
void Shutdown();

} // namespace Logging
} // namespace CommonUtils

#endif
