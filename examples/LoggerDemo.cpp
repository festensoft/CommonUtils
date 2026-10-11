// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "Demos.h"
#include "LoggerConfig.h"
#include "LoggerMacros.h"

#include <iostream>

void RunLoggerDemo()
{
   std::cout << "=== Logger demo ===\n";

   // Before InitFromConfig(): CommonUtils::Logging falls back to spdlog's
   // built-in default logger (colored console), so logging works from the
   // very first line of main() even if configuration happens later or
   // never happens at all.
   std::cout << "-- before InitFromConfig() (default fallback logger) --\n" << std::flush;
   GPINFO("Logger not yet configured; this goes to the default console logger.");
   GPWARN("Fallback logger warning example.");

   // Configure a real logger: console + rotating file sink, trace level,
   // a custom pattern.
   CommonUtils::Logging::LoggerConfig config;
   config.level = "trace";
   config.pattern = "[%H:%M:%S.%e] [%^%l%$] %v";

   CommonUtils::Logging::SinkConfig consoleSink;
   consoleSink.type = CommonUtils::Logging::SinkType::Console;
   consoleSink.level = "trace";
   consoleSink.colored = true;
   config.sinks.push_back(consoleSink);
   config.enableTraceLogger = true;
   config.traceBacktraceDepth = 256;
   config.traceFilePath = "trace.log";

   CommonUtils::Logging::SinkConfig fileSink;
   fileSink.type = CommonUtils::Logging::SinkType::RotatingFile;
   fileSink.level = "trace";
   fileSink.name = "example";
   fileSink.filePath = "example_app.log";
   fileSink.maxFileSizeBytes = 1024ul * 1024ul;
   fileSink.maxFiles = 3ul;
   config.sinks.push_back(fileSink);

   CommonUtils::Logging::InitFromConfig(config);

   std::cout << "-- after InitFromConfig() (console + rotating file sinks) --\n" << std::flush;
   GPTRACE("Trace message after configuration.");
   GPDEBUG("Debug value: {}", 42);
   GPINFO("Logger configured with {} sink(s).", config.sinks.size());
   GPWARN("This warning should also land in example_app.log.");
   GPERROR("Example error message.");
   GPCRIT("Example critical message.");

   CommonUtils::Logging::Flush();
   std::cout << "(also written to example_app.log via the rotating file sink)\n\n";
}
