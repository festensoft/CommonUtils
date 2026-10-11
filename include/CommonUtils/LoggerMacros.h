#ifndef COMMONUTILS_LOGGING_LOGGERMACROS_H_
#define COMMONUTILS_LOGGING_LOGGERMACROS_H_

#include "Logger.h"

// The first argument to every GP* macro must be a compile-time string
// literal (it's passed through as a std::format format string, which is
// validated against the remaining arguments at compile time). To log an
// arbitrary runtime string, pass it as an argument rather than as the
// format string itself, e.g. GPINFO("{}", dynamicString) -- never
// GPINFO(dynamicString), since that would try to use the dynamic content as
// format syntax.

#define GP_LOG_IMPL_(lvl_, ...)                                                            \
   do                                                                                      \
   {                                                                                       \
      if (::CommonUtils::Logging::ShouldLog(lvl_))                                         \
      {                                                                                    \
         ::CommonUtils::Logging::LogAt(                                                    \
             (lvl_), ::CommonUtils::Logging::SourceLocation(__FILE__, __LINE__, __func__), \
             ::CommonUtils::Logging::Detail::FormatMessage(__VA_ARGS__));                  \
      }                                                                                    \
   }                                                                                       \
   while (false)

#define GPCRIT(...) GP_LOG_IMPL_(::CommonUtils::Logging::Level::Critical, __VA_ARGS__)
#define GPERROR(...) GP_LOG_IMPL_(::CommonUtils::Logging::Level::Error, __VA_ARGS__)
#define GPWARN(...) GP_LOG_IMPL_(::CommonUtils::Logging::Level::Warn, __VA_ARGS__)
#define GPINFO(...) GP_LOG_IMPL_(::CommonUtils::Logging::Level::Info, __VA_ARGS__)
#define GPDEBUG(...) GP_LOG_IMPL_(::CommonUtils::Logging::Level::Debug, __VA_ARGS__)
#define GPTRACE(...) GP_LOG_IMPL_(::CommonUtils::Logging::Level::Trace, __VA_ARGS__)

#endif
