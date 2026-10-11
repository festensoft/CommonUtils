#include "SinkFactory.h"

#include "LogUtils.h"
#include "Logger.h"
#include "SinkRegistry.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/stdout_sinks.h"

#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace CommonUtils
{
namespace Logging
{
namespace
{

bool HasColorRangeMarkers(const std::string& pattern)
{
   return pattern.find("%^") != std::string::npos && pattern.find("%$") != std::string::npos;
}

Level FromSpdlogLevel(spdlog::level::level_enum level)
{
   switch (level)
   {
      case spdlog::level::trace:
         return Level::Trace;
      case spdlog::level::debug:
         return Level::Debug;
      case spdlog::level::info:
         return Level::Info;
      case spdlog::level::warn:
         return Level::Warn;
      case spdlog::level::err:
         return Level::Error;
      case spdlog::level::critical:
         return Level::Critical;
      case spdlog::level::off:
      case spdlog::level::n_levels:
         break;
   }
   return Level::Info;
}

class CustomSinkAdapter : public spdlog::sinks::base_sink<std::mutex>
{
public:
   explicit CustomSinkAdapter(std::shared_ptr<ICustomSink> customSink)
       : _sink(std::move(customSink))
   {
   }

protected:
   void sink_it_(const spdlog::details::log_msg& msg) override
   {
      if (!_sink)
      {
         return;
      }

      spdlog::memory_buf_t formattedBuffer;
      this->formatter_->format(msg, formattedBuffer);
      const std::string message(formattedBuffer.data(), formattedBuffer.size());
      _sink->Log(FromSpdlogLevel(msg.level), message);
   }

   void flush_() override
   {
      if (_sink)
      {
         _sink->Flush();
      }
   }

private:
   std::shared_ptr<ICustomSink> _sink;
};

std::shared_ptr<spdlog::sinks::sink> BuildRotatingFileSink(const std::string& filePath,
                                                           std::size_t maxFileSizeBytes,
                                                           std::size_t maxFiles)
{
   return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filePath, maxFileSizeBytes,
                                                                 maxFiles);
}

void AppendError(std::string* errorMessage, const std::string& text)
{
   if (errorMessage != nullptr)
   {
      *errorMessage += text + "; ";
   }
}

std::shared_ptr<spdlog::sinks::sink> BuildCustomSink(
    const SinkConfig& sinkConfig, const std::map<std::string, CustomSinkFactory>& customFactories,
    std::string* errorMessage)
{
   const auto found = customFactories.find(sinkConfig.name);
   if (found == customFactories.end() || !found->second)
   {
      AppendError(errorMessage, "skipping unregistered custom sink: " + sinkConfig.name);
      return nullptr;
   }

   const std::shared_ptr<ICustomSink> customSink = found->second(sinkConfig);
   if (!customSink)
   {
      AppendError(errorMessage, "custom sink factory returned null: " + sinkConfig.name);
      return nullptr;
   }

   return std::make_shared<CustomSinkAdapter>(customSink);
}

std::shared_ptr<spdlog::sinks::sink> BuildSink(
    const SinkConfig& sinkConfig, const std::map<std::string, CustomSinkFactory>& customFactories,
    std::string* errorMessage)
{
   switch (sinkConfig.type)
   {
      case SinkType::Console:
         if (sinkConfig.colored)
         {
            return std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
         }
         return std::make_shared<spdlog::sinks::stdout_sink_mt>();
      case SinkType::RotatingFile:
         return BuildRotatingFileSink(sinkConfig.filePath, sinkConfig.maxFileSizeBytes,
                                      sinkConfig.maxFiles);
      case SinkType::Custom:
         break;
   }
   return BuildCustomSink(sinkConfig, customFactories, errorMessage);
}

// Single return path on purpose, so the logger is constructed in place
// (named return value optimization) instead of copied.
std::shared_ptr<spdlog::logger> MakeLogger(const std::string& name,
                                           const std::vector<spdlog::sink_ptr>& sinks,
                                           const std::string& pattern,
                                           spdlog::level::level_enum level,
                                           std::size_t backtraceDepth)
{
   auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
   logger->set_pattern(pattern);
   logger->set_level(level);
   logger->flush_on(spdlog::level::err);
   if (backtraceDepth > 0)
   {
      logger->enable_backtrace(backtraceDepth);
   }
   return logger;
}

} // namespace

std::shared_ptr<spdlog::logger> BuildLogger(const LoggerConfig& config, std::string* errorMessage)
{
   if (errorMessage != nullptr)
   {
      errorMessage->clear();
   }

   spdlog::level::level_enum loggerLevel = spdlog::level::info;
   if (!TryParseLevel(config.level, &loggerLevel))
   {
      AppendError(errorMessage, "unknown logger level: " + config.level);
      return nullptr;
   }

   const std::map<std::string, CustomSinkFactory> customFactories = CopySinkRegistry();
   std::vector<spdlog::sink_ptr> sinks;
   bool hasColoredConsoleSink = false;
   for (const auto& sinkConfig : config.sinks)
   {
      spdlog::level::level_enum sinkLevel = spdlog::level::info;
      if (!TryParseLevel(sinkConfig.level, &sinkLevel))
      {
         AppendError(errorMessage, "unknown sink level: " + sinkConfig.level);
         continue;
      }

      const auto sink = BuildSink(sinkConfig, customFactories, errorMessage);
      if (!sink)
      {
         continue;
      }

      hasColoredConsoleSink =
          hasColoredConsoleSink || (sinkConfig.type == SinkType::Console && sinkConfig.colored);
      sink->set_level(sinkLevel);
      sinks.push_back(sink);
   }

   if (sinks.empty())
   {
      AppendError(errorMessage, "no usable sinks");
      return nullptr;
   }

   std::string loggerPattern = config.pattern;
   if (hasColoredConsoleSink && !HasColorRangeMarkers(loggerPattern))
   {
      loggerPattern = "%^" + loggerPattern + "%$";
   }
   return MakeLogger("general", sinks, loggerPattern, loggerLevel, 0);
}

std::shared_ptr<spdlog::logger> BuildTraceLogger(const LoggerConfig& config,
                                                 std::string* errorMessage)
{
   if (config.traceBacktraceDepth == 0)
   {
      AppendError(errorMessage, "traceBacktraceDepth must be greater than zero");
      return nullptr;
   }

   std::vector<spdlog::sink_ptr> sinks;
   sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
   if (!config.traceFilePath.empty())
   {
      const SinkConfig defaults;
      sinks.push_back(BuildRotatingFileSink(config.traceFilePath, defaults.maxFileSizeBytes,
                                            defaults.maxFiles));
   }

   // Level above Trace on purpose: nothing is written as it happens. spdlog
   // still records every message in the backtrace ring buffer regardless of
   // level, and writes them out on dump_backtrace().
   return MakeLogger("trace", sinks, config.pattern, spdlog::level::err,
                     config.traceBacktraceDepth);
}

} // namespace Logging
} // namespace CommonUtils
