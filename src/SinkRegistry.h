// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef LOGGING_SINKREGISTRY_H_
#define LOGGING_SINKREGISTRY_H_

#include "CustomSink.h"

#include <map>
#include <string>

namespace CommonUtils
{
namespace Logging
{

// Snapshot of every factory registered so far (see CustomSink.h for the
// public registration API). Internal: used by BuildLogger.
std::map<std::string, CustomSinkFactory> CopySinkRegistry();

} // namespace Logging
} // namespace CommonUtils

#endif
