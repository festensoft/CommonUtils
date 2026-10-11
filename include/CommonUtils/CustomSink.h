// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_LOGGING_CUSTOMSINK_H_
#define COMMONUTILS_LOGGING_CUSTOMSINK_H_

#include "Logger.h"
#include "SinkConfig.h"

#include <functional>
#include <memory>
#include <string>

namespace CommonUtils
{
namespace Logging
{

// Implemented by sink authors. One method covers every level -- an adapter
// maps every spdlog level onto it 1:1, so a sink can tell a warning from an
// error instead of having them collapsed together.
class ICustomSink
{
public:
   virtual ~ICustomSink() = default;
   virtual void Log(Level level, const std::string& message) = 0;

   virtual void Flush() {}
};

// Called by InitFromConfig for each SinkConfig of type SinkType::Custom whose
// `name` matches the name the factory was registered under. May return
// nullptr to signal that the sink can't be created; that sink is then skipped.
using CustomSinkFactory = std::function<std::shared_ptr<ICustomSink>(const SinkConfig& config)>;

// Register a factory under `sinkName`. Call before InitFromConfig, or use
// SinkRegistrar.
void RegisterCustomSinkFactory(const std::string& sinkName, const CustomSinkFactory& factory);

// Instantiate at namespace scope in a sink .cpp to auto-register before main().
// Note: if the sink lives in a static library and nothing else in its object
// file is referenced, the linker can drop this object entirely and
// registration silently vanishes. Force the object file to stay linked (e.g.
// an explicit reference from the consuming binary, an OBJECT library, or
// --whole-archive) if that applies to your build.
struct SinkRegistrar
{
   SinkRegistrar(const char* sinkName, const CustomSinkFactory& factory)
   {
      RegisterCustomSinkFactory(sinkName, factory);
   }
};

} // namespace Logging
} // namespace CommonUtils

#endif
