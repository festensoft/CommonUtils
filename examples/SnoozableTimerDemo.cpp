// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "Demos.h"
#include "LoggerMacros.h"
#include "SnoozableTimer.h"

#include <atomic>
#include <chrono>
#include <thread>

void RunSnoozableTimerDemo()
{
   GPINFO("=== SnoozableTimer demo ===");

   std::atomic<int> fireCount{0};
   CommonUtils::SnoozableTimer timer(
       [&fireCount]() {
          ++fireCount;
          GPINFO("  snoozable timer fired ({})", fireCount.load());
       },
       150);
   timer.Start();

   std::this_thread::sleep_for(std::chrono::milliseconds(80));
   GPINFO("snoozing before the initial 150ms period elapses...");
   timer.Snooze();

   std::this_thread::sleep_for(std::chrono::milliseconds(200));
   GPINFO("fireCount after snooze + wait: {} (fired once, ~150ms after the snooze() call, not the "
          "original start)",
          fireCount.load());

   timer.Stop();
}
