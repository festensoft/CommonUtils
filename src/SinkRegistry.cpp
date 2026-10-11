// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "SinkRegistry.h"

#include <map>
#include <mutex>

namespace CommonUtils
{
namespace Logging
{
namespace
{

// Genuine cross-TU ordering requirement, not a stand-in for the module's
// general state: custom sinks self-register via SinkRegistrar objects at
// static-init time in *other* translation units (e.g. a sink's own .cpp), so
// this registry must exist before any such object's constructor can touch
// it, regardless of link order. A function-local static is the standard,
// narrow idiom for exactly that problem.
std::map<std::string, CustomSinkFactory>& GetRegistry()
{
   static std::map<std::string, CustomSinkFactory> registry;
   return registry;
}

std::mutex& GetRegistryMutex()
{
   static std::mutex mutex;
   return mutex;
}

} // namespace

void RegisterCustomSinkFactory(const std::string& sinkName, const CustomSinkFactory& factory)
{
   const std::lock_guard<std::mutex> lock(GetRegistryMutex());
   GetRegistry()[sinkName] = factory;
}

std::map<std::string, CustomSinkFactory> CopySinkRegistry()
{
   const std::lock_guard<std::mutex> lock(GetRegistryMutex());
   return GetRegistry();
}

} // namespace Logging
} // namespace CommonUtils
