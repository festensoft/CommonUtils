// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "LogUtils.h"

#include <cctype>
#include <stdexcept>
#include <string_view>

namespace CommonUtils
{
namespace Logging
{

bool TryParseLevel(const std::string& levelText, spdlog::level::level_enum* level)
{
   if (levelText == LEVEL_TRACE)
   {
      *level = spdlog::level::trace;
   }
   else if (levelText == LEVEL_DEBUG)
   {
      *level = spdlog::level::debug;
   }
   else if (levelText == LEVEL_INFO)
   {
      *level = spdlog::level::info;
   }
   else if (levelText == LEVEL_WARN)
   {
      *level = spdlog::level::warn;
   }
   else if (levelText == LEVEL_ERROR)
   {
      *level = spdlog::level::err;
   }
   else if (levelText == LEVEL_CRITICAL)
   {
      *level = spdlog::level::critical;
   }
   else if (levelText == LEVEL_OFF)
   {
      *level = spdlog::level::off;
   }
   else
   {
      return false;
   }

   return true;
}

spdlog::level::level_enum ToSpdlogLevel(const std::string& levelText)
{
   spdlog::level::level_enum level = spdlog::level::off;
   if (!TryParseLevel(levelText, &level))
   {
      throw std::runtime_error("unknown log level: " + levelText);
   }
   return level;
}

spdlog::level::level_enum ToSpdlogLevel(Level level)
{
   switch (level)
   {
      case Level::Trace:
         return spdlog::level::trace;
      case Level::Debug:
         return spdlog::level::debug;
      case Level::Info:
         return spdlog::level::info;
      case Level::Warn:
         return spdlog::level::warn;
      case Level::Error:
         return spdlog::level::err;
      case Level::Critical:
         return spdlog::level::critical;
   }
   return spdlog::level::info;
}

} // namespace Logging
} // namespace CommonUtils
