// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "Demos.h"
#include "LoggerMacros.h"
#include "Timer.h"

#include <atomic>
#include <chrono>
#include <thread>

void RunTimerDemo()
{
   GPINFO("=== Timer demo ===");

   CommonUtils::Timer oneShot;
   std::atomic<bool> fired{false};
   oneShot.StartOneShot(
       [&fired]() {
          fired = true;
          GPINFO("  one-shot fired");
       },
       100);
   std::this_thread::sleep_for(std::chrono::milliseconds(150));
   GPINFO("one-shot fired= {}", fired.load());
   ;

   CommonUtils::Timer periodic;
   std::atomic<int> ticks{0};
   periodic.StartPeriodic(
       [&ticks]() {
          ++ticks;
          GPINFO("  periodic tick {}", ticks.load());
       },
       80);
   std::this_thread::sleep_for(std::chrono::milliseconds(300));
   periodic.Stop();
   GPINFO("periodic ticks={}", ticks.load());
}
