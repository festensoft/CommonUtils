#ifndef LOGGING_SINKFACTORY_H_
#define LOGGING_SINKFACTORY_H_

#include "LoggerConfig.h"

#include <memory>
#include <string>

#include "spdlog/logger.h"

namespace CommonUtils
{
namespace Logging
{

// Both builders return nullptr on failure and append a description of what
// went wrong (unregistered custom sink, bad level, etc.) to *errorMessage
// rather than writing to stderr. They may throw spdlog::spdlog_ex if a sink
// can't be created (e.g. an unwritable log file).

// The general logger: one spdlog sink per usable entry in config.sinks.
std::shared_ptr<spdlog::logger> BuildLogger(const LoggerConfig& config,
                                            std::string* errorMessage = nullptr);

// The trace logger described by config.enableTraceLogger and friends.
std::shared_ptr<spdlog::logger> BuildTraceLogger(const LoggerConfig& config,
                                                 std::string* errorMessage = nullptr);

} // namespace Logging
} // namespace CommonUtils

#endif
