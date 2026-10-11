// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "CustomSink.h"
#include "LoggerMacros.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

namespace Logging = CommonUtils::Logging;

namespace
{

std::string ReadFile(const std::filesystem::path& path)
{
   const std::ifstream stream(path);
   std::stringstream contents;
   contents << stream.rdbuf();
   return contents.str();
}

class LoggerTest : public ::testing::Test
{
protected:
   void SetUp() override
   {
      Logging::Shutdown();
      _dir = std::filesystem::temp_directory_path() /
             (std::string("CommonUtilsLoggerUt_") +
              ::testing::UnitTest::GetInstance()->current_test_info()->name());
      std::filesystem::remove_all(_dir);
      std::filesystem::create_directories(_dir);
   }

   void TearDown() override
   {
      Logging::Shutdown();
      std::filesystem::remove_all(_dir);
   }

   [[nodiscard]] Logging::LoggerConfig FileConfig(const std::string& level = "info") const
   {
      Logging::SinkConfig sink;
      sink.type = Logging::SinkType::RotatingFile;
      sink.level = "trace";
      sink.filePath = (_dir / "general.log").string();

      Logging::LoggerConfig config;
      config.level = level;
      config.pattern = "%l %v";
      config.sinks.push_back(sink);
      return config;
   }

   std::filesystem::path _dir;
};

TEST_F(LoggerTest, FallbackLoggerWorksBeforeInit)
{
   EXPECT_FALSE(Logging::IsInitialized());
   EXPECT_TRUE(Logging::ShouldLog(Logging::Level::Info));
   GPINFO("fallback logger {}", 1);
}

TEST_F(LoggerTest, InitFromConfigWritesToFileAndHonorsLevel)
{
   ASSERT_TRUE(Logging::InitFromConfig(FileConfig("warn")));
   EXPECT_TRUE(Logging::IsInitialized());
   EXPECT_FALSE(Logging::ShouldLog(Logging::Level::Info));

   GPINFO("should be filtered");
   GPWARN("answer is {}", 42);
   Logging::Flush();

   const std::string contents = ReadFile(_dir / "general.log");
   EXPECT_NE(contents.find("warning answer is 42"), std::string::npos);
   EXPECT_EQ(contents.find("filtered"), std::string::npos);
}

TEST_F(LoggerTest, ConfigWithoutSinksFailsAndKeepsFallback)
{
   const Logging::LoggerConfig config;
   EXPECT_FALSE(Logging::InitFromConfig(config));
   EXPECT_FALSE(Logging::IsInitialized());
}

TEST_F(LoggerTest, UnknownLevelFails)
{
   EXPECT_FALSE(Logging::InitFromConfig(FileConfig("loud")));
   EXPECT_FALSE(Logging::IsInitialized());
}

TEST_F(LoggerTest, UnwritableFileFailsWithoutThrowing)
{
   Logging::LoggerConfig config = FileConfig();
   // A directory can't be opened as a log file.
   config.sinks[0].filePath = _dir.string();
   EXPECT_FALSE(Logging::InitFromConfig(config));
   EXPECT_FALSE(Logging::IsInitialized());
}

TEST_F(LoggerTest, TraceIsDroppedWhenTraceLoggerDisabled)
{
   ASSERT_TRUE(Logging::InitFromConfig(FileConfig("info")));
   EXPECT_FALSE(Logging::ShouldLog(Logging::Level::Trace));
   GPTRACE("never seen");
   Logging::DumpTraceBacktrace(); // no-op, must not crash
   Logging::Flush();
   EXPECT_EQ(ReadFile(_dir / "general.log").find("never seen"), std::string::npos);
}

TEST_F(LoggerTest, TraceLoggerRecordsEveryLevelUntilDumped)
{
   Logging::LoggerConfig config = FileConfig("warn");
   config.enableTraceLogger = true;
   config.traceBacktraceDepth = 3;
   config.traceFilePath = (_dir / "trace.log").string();
   ASSERT_TRUE(Logging::InitFromConfig(config));
   // Everything is recorded, so nothing may be skipped, even below "warn".
   EXPECT_TRUE(Logging::ShouldLog(Logging::Level::Trace));
   EXPECT_TRUE(Logging::ShouldLog(Logging::Level::Debug));

   GPTRACE("first");
   GPTRACE("second");
   GPDEBUG("third");
   GPINFO("fourth");
   GPERROR("fifth");
   Logging::Flush();

   // Nothing is written from the ring buffer until the dump...
   EXPECT_EQ(ReadFile(_dir / "trace.log"), "");
   // ...and the general log behaves as if the recorder weren't there:
   // warn and above only.
   const std::string general = ReadFile(_dir / "general.log");
   EXPECT_NE(general.find("fifth"), std::string::npos);
   EXPECT_EQ(general.find("fourth"), std::string::npos);
   EXPECT_EQ(general.find("third"), std::string::npos);

   Logging::DumpTraceBacktrace();

   // Depth is 3: the two oldest are gone; debug, info and error all made it,
   // each with its original level.
   const std::string trace = ReadFile(_dir / "trace.log");
   EXPECT_EQ(trace.find("first"), std::string::npos);
   EXPECT_EQ(trace.find("second"), std::string::npos);
   EXPECT_NE(trace.find("third"), std::string::npos);
   EXPECT_NE(trace.find("fourth"), std::string::npos);
   EXPECT_NE(trace.find("fifth"), std::string::npos);
   EXPECT_NE(trace.find("debug third"), std::string::npos);
   EXPECT_NE(trace.find("error fifth"), std::string::npos);
}

TEST_F(LoggerTest, ZeroTraceDepthIsRejected)
{
   Logging::LoggerConfig config = FileConfig();
   config.enableTraceLogger = true;
   config.traceBacktraceDepth = 0;
   EXPECT_FALSE(Logging::InitFromConfig(config));
}

class CapturingSink : public Logging::ICustomSink
{
public:
   void Log(Logging::Level level, const std::string& message) override
   {
      lastLevel = level;
      messages.push_back(message);
   }

   Logging::Level lastLevel = Logging::Level::Info;
   std::vector<std::string> messages;
};

TEST_F(LoggerTest, CustomSinkReceivesMessagesWithLevel)
{
   const auto sink = std::make_shared<CapturingSink>();
   Logging::RegisterCustomSinkFactory(
       "capture", [sink](const Logging::SinkConfig&)
       { return std::shared_ptr<Logging::ICustomSink>(sink); });

   Logging::SinkConfig sinkConfig;
   sinkConfig.type = Logging::SinkType::Custom;
   sinkConfig.name = "capture";
   sinkConfig.level = "trace";

   Logging::LoggerConfig config;
   config.pattern = "%v";
   config.sinks.push_back(sinkConfig);
   ASSERT_TRUE(Logging::InitFromConfig(config));

   GPERROR("boom {}", 7);
   ASSERT_EQ(sink->messages.size(), 1U);
   EXPECT_EQ(sink->lastLevel, Logging::Level::Error);
   EXPECT_NE(sink->messages[0].find("boom 7"), std::string::npos);
}

TEST_F(LoggerTest, UnregisteredCustomSinkFails)
{
   Logging::SinkConfig sinkConfig;
   sinkConfig.type = Logging::SinkType::Custom;
   sinkConfig.name = "nobody-registered-this";

   Logging::LoggerConfig config;
   config.sinks.push_back(sinkConfig);
   EXPECT_FALSE(Logging::InitFromConfig(config));
}

} // namespace
