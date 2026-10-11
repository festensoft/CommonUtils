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

   // Optional trace logger, intended for post-mortem diagnostics. When
   // enabled, GPTRACE messages are routed here instead of the general logger.
   // They are never written as they happen; they are kept in a ring buffer of
   // the last traceBacktraceDepth messages and written out only when
   // DumpTraceBacktrace() is called (typically from a crash handler).
   // The dump goes to the console and, if traceFilePath is set, to that
   // rotating file. It is independent of the general logger's sinks and levels.
   bool enableTraceLogger = false;
   std::size_t traceBacktraceDepth = 128;
   std::string traceFilePath;
};

} // namespace Logging
} // namespace CommonUtils

#endif
