#ifndef LOGGING_SINKCONFIG_H_
#define LOGGING_SINKCONFIG_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace CommonUtils
{
namespace Logging
{

enum class SinkType : std::uint8_t
{
   Console,
   RotatingFile,
   Custom
};

struct SinkConfig
{
   SinkType type = SinkType::Console;
   std::string level = "info";
   bool colored = false;
   std::string name;
   std::string filePath;
   std::size_t maxFileSizeBytes = 1024ul * 1024ul * 5ul;
   std::size_t maxFiles = 3;
};

} // namespace Logging
} // namespace CommonUtils

#endif