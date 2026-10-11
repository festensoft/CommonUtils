// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_LOGGING_LOGGERCONFIG_H_
#define COMMONUTILS_LOGGING_LOGGERCONFIG_H_

#include "SinkConfig.h"

#include <cstddef>
#include <string>
#include <vector>

namespace CommonUtils
{
namespace Logging
{

struct LoggerConfig
{
   std::string level = "info";
   std::string pattern = "%H%M%S.%e [%t] %v";
   std::vector<SinkConfig> sinks;

   // Optional trace logger: a "flight recorder" for post-mortem diagnostics.
   // When enabled, EVERY message at every level (trace through critical) is
   // also kept in a ring buffer of the last traceBacktraceDepth messages,
   // regardless of the logger's or sinks' levels. Nothing is written from the
   // buffer as it happens; it is written out only when DumpTraceBacktrace()
   // is called (typically from a crash handler). The general logger is not
   // affected and filters by its own levels as usual.
   // The dump goes to the console and, if traceFilePath is set, to that
   // rotating file, independent of the general logger's sinks and levels.
   // Cost: every GP* call is formatted and recorded, even ones the general
   // logger would have skipped.
   bool enableTraceLogger = false;
   std::size_t traceBacktraceDepth = 128;
   std::string traceFilePath;
};

} // namespace Logging
} // namespace CommonUtils

#endif
