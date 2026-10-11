// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef LOGGING_LOGUTILS_H_
#define LOGGING_LOGUTILS_H_

#include "Logger.h"

#include <spdlog/common.h>

#include <string>
#include <string_view>

namespace CommonUtils
{
namespace Logging
{

constexpr std::string_view LEVEL_TRACE = "trace";
constexpr std::string_view LEVEL_DEBUG = "debug";
constexpr std::string_view LEVEL_INFO = "info";
constexpr std::string_view LEVEL_WARN = "warn";
constexpr std::string_view LEVEL_ERROR = "error";
constexpr std::string_view LEVEL_CRITICAL = "critical";
constexpr std::string_view LEVEL_OFF = "off";

// Non-throwing: returns false and leaves *level untouched on an unrecognized
// string. Prefer this over ToSpdlogLevel wherever the caller can't let an
// exception escape (e.g. through a bool-returning API).
bool TryParseLevel(const std::string& levelText, spdlog::level::level_enum* level);

// Throws std::runtime_error on an unrecognized string. Kept for callers that
// have already validated the string (e.g. the YAML reader) or that are fine
// with an exception.
spdlog::level::level_enum ToSpdlogLevel(const std::string& levelText);

spdlog::level::level_enum ToSpdlogLevel(Level level);

} // namespace Logging
} // namespace CommonUtils

#endif
